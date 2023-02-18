// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <span>
#include <vector>
#include "common/common_types.h"
#include "common/dds-ktx.h"

namespace Common {

bool ParsePNG(std::span<const u8> png_data, size_t& decoded_size, u32& width, u32& height);

bool DecodePNG(std::span<const u8> png_data, std::span<u8> out_data);

bool ParseDDSKTX(std::span<const u8> in_data, std::vector<u8>& out_data, u32& width, u32& height,
                 ddsktx_format& format);

bool EncodePNG(const std::string& out_path, std::span<u8> in_data, u32 width, u32 height,
               s32 level = 6);

void FlipTexture(std::span<u8> in_data, u32 width, u32 height, u32 stride);

} // namespace Common
