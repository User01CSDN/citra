// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/frontend/image_interface.h"

class QtImageInterface final : public Frontend::ImageInterface {
public:
    bool DecodePNG(std::span<const u8> src, std::vector<u8>& dst, u32& width, u32& height) override;
    bool EncodePNG(const std::string& path, std::span<const u8> src, u32 width,
                   u32 height) override;
};
