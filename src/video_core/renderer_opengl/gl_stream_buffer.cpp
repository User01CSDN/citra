// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace OpenGL {

StreamBuffer::StreamBuffer(const Driver& driver_, GLenum target, size_t size_)
    : driver{driver_}, gl_target{target}, buffer_size{size_}, slot_size{buffer_size / SYNC_POINTS},
      buffer_storage{driver.HasBufferStorage()} {
    for (size_t i = 0; i < SYNC_POINTS; i++) {
        fences[i].Create();
    }

    gl_buffer.Create();
    glBindBuffer(gl_target, gl_buffer.handle);

    if (buffer_storage) {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        if (driver.HasExtBufferStorage()) {
            glBufferStorageEXT(gl_target, buffer_size, nullptr, flags);
        } else {
            glBufferStorage(gl_target, buffer_size, nullptr, flags);
        }

        mapped_ptr =
            (u8*)glMapBufferRange(gl_target, 0, buffer_size,
                                  GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
    } else {
        glBufferData(gl_target, buffer_size, nullptr, GL_STREAM_DRAW);
    }
}

StreamBuffer::~StreamBuffer() {
    if (buffer_storage) {
        glBindBuffer(gl_target, gl_buffer.handle);
        glUnmapBuffer(gl_target);
    }
}

std::tuple<u8*, size_t, bool> StreamBuffer::Map(size_t size, size_t alignment) {
    mapped_size = size;

    if (alignment > 0) {
        iterator = Common::AlignUp(iterator, alignment);
    }

    for (std::size_t slot = Slot(used_iterator), slot_end = Slot(iterator); slot < slot_end;
         ++slot) {
        fences[slot].Create();
    }
    used_iterator = iterator;

    for (std::size_t slot = Slot(free_iterator) + 1,
                     slot_end = std::min(Slot(iterator + size) + 1, SYNC_POINTS);
         slot < slot_end; ++slot) {
        glClientWaitSync(fences[slot].handle, 0, GL_TIMEOUT_IGNORED);
        fences[slot].Release();
    }
    if (iterator + size >= free_iterator) {
        free_iterator = iterator + size;
    }

    bool invalidate{false};
    if (iterator + size > buffer_size) {
        for (std::size_t slot = Slot(used_iterator); slot < SYNC_POINTS; ++slot) {
            fences[slot].Create();
        }
        invalidate = true;
        used_iterator = 0;
        iterator = 0;
        free_iterator = size;

        for (std::size_t slot = 0, slot_end = Slot(size); slot <= slot_end; ++slot) {
            glClientWaitSync(fences[slot].handle, 0, GL_TIMEOUT_IGNORED);
            fences[slot].Release();
        }
    }

    u8* pointer{};
    if (buffer_storage) {
        pointer = mapped_ptr + iterator;
    } else {
        pointer = (u8*)glMapBufferRange(gl_target, iterator, size,
                                        GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT |
                                            GL_MAP_UNSYNCHRONIZED_BIT);
    }

    return std::make_tuple(pointer, iterator, invalidate);
}

void StreamBuffer::Unmap(size_t used_size) {
    ASSERT_MSG(used_size <= mapped_size, "Reserved size {} is too small compared to {}",
               mapped_size, used_size);

    if (!buffer_storage) {
        glFlushMappedBufferRange(gl_target, 0, used_size);
        glUnmapBuffer(gl_target);
    }
    iterator += used_size;
}

} // namespace OpenGL
