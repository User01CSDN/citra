// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <tuple>
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class Driver;

class StreamBuffer {
    static constexpr std::size_t SYNC_POINTS = 16;

public:
    StreamBuffer(const Driver& driver, GLenum target, size_t size);
    ~StreamBuffer();

    [[nodiscard]] GLuint Handle() const noexcept {
        return gl_buffer.handle;
    }

    [[nodiscard]] size_t Size() const noexcept {
        return buffer_size;
    }

    /* This mapping function will return a pair of:
     * - the pointer to the mapped buffer
     * - the offset into the real GPU buffer (always multiple of stride)
     * On mapping, the maximum of size for allocation has to be set.
     * The size really pushed into this fifo only has to be known on Unmapping.
     * Mapping invalidates the current buffer content,
     * so it isn't allowed to access the old content any more.
     */
    std::tuple<u8*, u64, bool> Map(u64 size, u64 alignment = 0);
    void Unmap(u64 used_size);

private:
    [[nodiscard]] u64 Slot(u64 offset) noexcept {
        return offset / slot_size;
    }

    const Driver& driver;
    GLenum gl_target;
    size_t buffer_size;
    size_t slot_size;
    bool buffer_storage{};
    u8* mapped_ptr{};
    u64 mapped_size;

    u64 iterator = 0;
    u64 used_iterator = 0;
    u64 free_iterator = 0;

    OGLBuffer gl_buffer;
    std::array<OGLSync, SYNC_POINTS> fences{};
};

} // namespace OpenGL
