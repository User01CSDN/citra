// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#define DDSKTX_IMPLEMENT

#include <lodepng.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/frontend/image_interface.h"

namespace Frontend {

bool ImageInterface::DecodePNG(std::vector<u8>& dst, u32& width, u32& height,
                               const std::string& path) {
    u32 lodepng_ret = lodepng::decode(dst, width, height, path);
    if (lodepng_ret) {
        LOG_CRITICAL(Frontend, "Failed to decode {} because {}", path,
                     lodepng_error_text(lodepng_ret));
        return false;
    }
    return true;
}

bool ImageInterface::DecodeDDS(std::vector<u8>& dst, u32& width, u32& height, ddsktx_format& format,
                               const std::string& path) {
    FileUtil::IOFile file{path, "rb"};
    if (!file.IsOpen()) {
        return false;
    }

    const int dds_size = static_cast<int>(file.GetSize());
    std::vector<u8> dds_data(dds_size);
    if (file.ReadBytes(dds_data.data(), dds_size) != dds_size) {
        return false;
    }

    ddsktx_texture_info tc{};
    if (!ddsktx_parse(&tc, dds_data.data(), dds_size, nullptr)) {
        LOG_CRITICAL(Frontend, "Failed to decode {}", path);
        return false;
    }

    width = tc.width;
    height = tc.height;
    format = tc.format;

    ddsktx_sub_data sub_data{};
    ddsktx_get_sub(&tc, &sub_data, dds_data.data(), dds_size, 0, 0, 0);

    dst.resize(sub_data.size_bytes);
    std::memcpy(dst.data(), sub_data.buff, sub_data.size_bytes);

    return true;
}

bool ImageInterface::EncodePNG(const std::string& path, std::span<const u8> src, u32 width,
                               u32 height) {
    u32 lodepng_ret = lodepng::encode(path, src.data(), width, height);
    if (lodepng_ret) {
        LOG_CRITICAL(Frontend, "Failed to encode {} because {}", path,
                     lodepng_error_text(lodepng_ret));
        return false;
    }
    return true;
}

} // namespace Frontend
