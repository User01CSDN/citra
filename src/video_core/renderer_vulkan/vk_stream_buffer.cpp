// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <limits>
#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace {

VmaAllocationCreateFlags MakeVMAFlags(BufferType type) {
    switch (type) {
    case BufferType::Upload:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    case BufferType::Download:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    case BufferType::Stream:
        return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
    }
}

constexpr u64 WATCHES_INITIAL_RESERVE = 0x4000;
constexpr u64 WATCHES_RESERVE_CHUNK = 0x1000;

} // Anonymous namespace

StreamBuffer::StreamBuffer(const Instance& instance_, Scheduler& scheduler_,
                           vk::BufferUsageFlags usage_, u64 size, BufferType type_)
    : instance{instance_}, scheduler{scheduler_},
      stream_buffer_size{size}, usage{usage_}, type{type_} {
    CreateBuffers(size);
    ReserveWatches(current_watches, WATCHES_INITIAL_RESERVE);
    ReserveWatches(previous_watches, WATCHES_INITIAL_RESERVE);
}

StreamBuffer::~StreamBuffer() {
    vmaDestroyBuffer(instance.GetAllocator(), buffer, allocation);
}

std::tuple<u8*, u64, bool> StreamBuffer::Map(u64 size, u64 alignment) {
    ASSERT(size <= stream_buffer_size);
    mapped_size = size;

    if (alignment > 0) {
        offset = Common::AlignUp(offset, alignment);
    }

    bool invalidate{false};
    if (offset + size > stream_buffer_size) {
        // The buffer would overflow, save the amount of used watches and reset the state.
        invalidate = true;
        invalidation_mark = current_watch_cursor;
        current_watch_cursor = 0;
        offset = 0;

        // Swap watches and reset waiting cursors.
        std::swap(previous_watches, current_watches);
        wait_cursor = 0;
        wait_bound = 0;
    }

    const u64 mapped_upper_bound = offset + size;
    WaitPendingOperations(mapped_upper_bound);

    return std::make_tuple(mapped + offset, offset, invalidate);
}

void StreamBuffer::Commit(u64 size) {
    ASSERT_MSG(size <= mapped_size, "Reserved size {} is too small compared to {}", mapped_size,
               size);

    offset += size;

    if (current_watch_cursor + 1 >= current_watches.size()) {
        // Ensure that there are enough watches.
        ReserveWatches(current_watches, WATCHES_RESERVE_CHUNK);
    }
    auto& watch = current_watches[current_watch_cursor++];
    watch.upper_bound = offset;
    watch.tick = scheduler.CurrentTick();
}

void StreamBuffer::CreateBuffers(u64 prefered_size) {
    const vk::BufferCreateInfo buffer_info = {
        .size = prefered_size,
        .usage = usage,
    };

    const VmaAllocationCreateInfo alloc_create_info = {
        .flags = MakeVMAFlags(type) | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VkBuffer unsafe_buffer{};
    VkBufferCreateInfo unsafe_buffer_info = static_cast<VkBufferCreateInfo>(buffer_info);
    VmaAllocationInfo alloc_info{};
    vmaCreateBuffer(instance.GetAllocator(), &unsafe_buffer_info, &alloc_create_info,
                    &unsafe_buffer, &allocation, &alloc_info);
    buffer = vk::Buffer{unsafe_buffer};

    VkMemoryPropertyFlags memory_flags{};
    vmaGetAllocationMemoryProperties(instance.GetAllocator(), allocation, &memory_flags);
    if (type == BufferType::Stream) {
        ASSERT_MSG(memory_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                   "Stream buffer must be host visible!");
        if (!(memory_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            LOG_WARNING(Render_Vulkan,
                        "Unable to use device local memory for stream buffer. It will be slower!");
        }
    }

    mapped = reinterpret_cast<u8*>(alloc_info.pMappedData);
}

void StreamBuffer::ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size) {
    watches.resize(watches.size() + grow_size);
}

void StreamBuffer::WaitPendingOperations(u64 requested_upper_bound) {
    if (!invalidation_mark) {
        return;
    }
    while (requested_upper_bound > wait_bound && wait_cursor < *invalidation_mark) {
        auto& watch = previous_watches[wait_cursor];
        wait_bound = watch.upper_bound;
        scheduler.Wait(watch.tick);
        ++wait_cursor;
    }
}

} // namespace Vulkan
