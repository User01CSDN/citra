// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <span>
#include <tuple>
#include <vector>
#include "video_core/renderer_vulkan/vk_common.h"

VK_DEFINE_HANDLE(VmaAllocation)

namespace Vulkan {

enum class BufferType : u32 {
    Upload = 0,
    Download = 1,
    Stream = 2,
};

class Instance;
class Scheduler;

class StreamBuffer final {
    static constexpr std::size_t MAX_BUFFER_VIEWS = 3;

public:
    explicit StreamBuffer(const Instance& instance, Scheduler& scheduler,
                          vk::BufferUsageFlags usage, u64 size,
                          BufferType type = BufferType::Stream);
    ~StreamBuffer();

    /**
     * Reserves a region of memory from the stream buffer.
     * @param size Size to reserve.
     * @returns A pair of a raw memory pointer (with offset added), and the buffer offset
     */
    std::tuple<u8*, u64, bool> Map(u64 size, u64 alignment);

    /// Ensures that "size" bytes of memory are available to the GPU, potentially recording a copy.
    void Commit(u64 size);

    vk::Buffer Handle() const noexcept {
        return buffer;
    }

    u64 Address() const noexcept {
        return 0;
    }

private:
    struct Watch {
        u64 tick{};
        u64 upper_bound{};
    };

    /// Creates Vulkan buffer handles committing the required the required memory.
    void CreateBuffers(u64 prefered_size);

    /// Increases the amount of watches available.
    void ReserveWatches(std::vector<Watch>& watches, std::size_t grow_size);

    void WaitPendingOperations(u64 requested_upper_bound);

private:
    const Instance& instance; ///< Vulkan instance.
    Scheduler& scheduler;     ///< Command scheduler.

    vk::Buffer buffer;        ///< Mapped buffer.
    VmaAllocation allocation; ///< VMA allocation
    u8* mapped{};             ///< Pointer to the mapped memory
    u64 stream_buffer_size{}; ///< Stream buffer size.
    vk::BufferUsageFlags usage{};
    BufferType type;

    u64 offset{};      ///< Buffer iterator.
    u64 mapped_size{}; ///< Size reserved for the current copy.

    std::vector<Watch> current_watches;           ///< Watches recorded in the current iteration.
    std::size_t current_watch_cursor{};           ///< Count of watches, reset on invalidation.
    std::optional<std::size_t> invalidation_mark; ///< Number of watches used in the previous cycle.

    std::vector<Watch> previous_watches; ///< Watches used in the previous iteration.
    std::size_t wait_cursor{};           ///< Last watch being waited for completion.
    u64 wait_bound{};                    ///< Highest offset being watched for completion.
};

} // namespace Vulkan
