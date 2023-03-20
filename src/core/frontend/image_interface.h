// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/dds-ktx.h"

namespace Frontend {

/**
 * Utility class that provides image decoding/encoding to the custom texture manager.
 * Can be optionally overriden by frontends to provide a custom implementation.
 */
class ImageInterface {
public:
    virtual ~ImageInterface() = default;

    virtual bool DecodePNG(std::vector<u8>& dst, u32& width, u32& height, const std::string& path);
    virtual bool DecodeDDS(std::vector<u8>& dst, u32& width, u32& height, ddsktx_format& format,
                           const std::string& path);
    virtual bool EncodePNG(const std::string& path, std::span<const u8> src, u32 width, u32 height);
};

} // namespace Frontend
