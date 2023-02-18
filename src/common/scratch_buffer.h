// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <span>
#include <type_traits>

namespace Common {

/**
 * A ScratchBuffer is a simple heap allocated array without member initialization.
 * Main usage is for temporary buffers passed to threads for example
 */
template <typename T>
class ScratchBuffer {
    static_assert(std::is_trivial_v<T>, "Must use a POD type");

public:
    ScratchBuffer(std::size_t size_) : size{size_} {
        buffer = std::unique_ptr<T>(new typename std::remove_extent<T>::type[size]);
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return size;
    }

    [[nodiscard]] T* Data() const noexcept {
        return buffer.get();
    }

    [[nodiscard]] std::span<const T> Span(u32 index = 0) const noexcept {
        return std::span<const T>{buffer.get() + index, size - index};
    }

    [[nodiscard]] std::span<T> Span(u32 index = 0) noexcept {
        return std::span<T>{buffer.get() + index, size - index};
    }

private:
    std::unique_ptr<T> buffer;
    std::size_t size;
};

} // namespace Common
