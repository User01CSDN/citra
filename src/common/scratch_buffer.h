// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <type_traits>
#include <span>
#include <memory>
#include "common/common_types.h"

namespace Common {

template <typename T, u32 alignment = 0>
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

    [[nodiscard]] std::span<const T> Span() const noexcept {
        return std::span<const T>{buffer.get(), size};
    }

private:
    std::unique_ptr<T> buffer;
    std::size_t size;
};

} // namespace Common
