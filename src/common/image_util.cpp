// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <spng.h>
#define DDSKTX_IMPLEMENT
#include "common/file_util.h"
#include "common/image_util.h"
#include "common/logging/log.h"

namespace Common {

namespace {

void spng_free(spng_ctx* ctx) {
    if (ctx) {
        spng_ctx_free(ctx);
    }
}

auto make_spng_ctx(int flags) {
    return std::unique_ptr<spng_ctx, decltype(&spng_free)>(spng_ctx_new(flags), spng_free);
}

} // Anonymous namespace

bool ParsePNG(std::span<const u8> png_data, size_t& decoded_size, u32& width, u32& height) {
    auto ctx = make_spng_ctx(0);
    if (!ctx) [[unlikely]] {
        return false;
    }

    if (spng_set_png_buffer(ctx.get(), png_data.data(), png_data.size())) {
        return false;
    }

    spng_ihdr ihdr{};
    if (spng_get_ihdr(ctx.get(), &ihdr)) {
        return false;
    }
    width = ihdr.width;
    height = ihdr.height;

    const int format = SPNG_FMT_RGBA8;
    if (spng_decoded_image_size(ctx.get(), format, &decoded_size)) {
        return false;
    }

    return true;
}

bool DecodePNG(std::span<const u8> png_data, std::span<u8> out_data) {
    auto ctx = make_spng_ctx(0);
    if (!ctx) [[unlikely]] {
        return false;
    }

    if (spng_set_png_buffer(ctx.get(), png_data.data(), png_data.size())) {
        return false;
    }

    const int format = SPNG_FMT_RGBA8;
    size_t decoded_len = 0;
    if (spng_decoded_image_size(ctx.get(), format, &decoded_len)) {
        return false;
    }
    ASSERT(out_data.size() == decoded_len);

    if (spng_decode_image(ctx.get(), out_data.data(), decoded_len, format, 0)) {
        return false;
    }

    return true;
}

bool ParseDDSKTX(std::span<const u8> in_data, std::vector<u8>& out_data, u32& width, u32& height,
                 ddsktx_format& format) {
    ddsktx_texture_info tc{};
    const int size = static_cast<int>(in_data.size());
    if (!ddsktx_parse(&tc, in_data.data(), size, nullptr)) {
        return false;
    }

    width = tc.width;
    height = tc.height;
    format = tc.format;

    ddsktx_sub_data sub_data{};
    ddsktx_get_sub(&tc, &sub_data, in_data.data(), size, 0, 0, 0);

    out_data.resize(sub_data.size_bytes);
    std::memcpy(out_data.data(), sub_data.buff, sub_data.size_bytes);

    return true;
}

bool EncodePNG(const std::string& out_path, std::span<u8> in_data, u32 width, u32 height,
               s32 level) {
    auto ctx = make_spng_ctx(SPNG_CTX_ENCODER);
    if (!ctx) [[unlikely]] {
        return false;
    }

    if (spng_set_option(ctx.get(), SPNG_IMG_COMPRESSION_LEVEL, level)) {
        return false;
    }
    if (spng_set_option(ctx.get(), SPNG_ENCODE_TO_BUFFER, 1)) {
        return false;
    }

    spng_ihdr ihdr{};
    ihdr.width = width;
    ihdr.height = height;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.bit_depth = 8;
    if (spng_set_ihdr(ctx.get(), &ihdr)) {
        return false;
    }

    if (spng_encode_image(ctx.get(), in_data.data(), in_data.size(), SPNG_FMT_PNG,
                          SPNG_ENCODE_FINALIZE)) {
        return false;
    }

    int ret{};
    size_t png_size{};
    u8* png_buf = reinterpret_cast<u8*>(spng_get_png_buffer(ctx.get(), &png_size, &ret));

    if (!png_buf) {
        return false;
    }

    auto file = FileUtil::IOFile(out_path, "wb");
    file.WriteBytes(png_buf, png_size);

    size_t image_len = 0;
    spng_decoded_image_size(ctx.get(), SPNG_FMT_PNG, &image_len);
    LOG_ERROR(Common, "{} byte {} by {} image saved to {} at level {}", image_len, width, height,
              out_path, level);

    return true;
}

void FlipTexture(std::span<u8> in_data, u32 width, u32 height, u32 stride) {
    for (u32 line = 0; line < height / 2; line++) {
        const u32 offset_1 = line * stride;
        const u32 offset_2 = (height - line - 1) * stride;
        // Swap lines
        std::swap_ranges(in_data.begin() + offset_1, in_data.begin() + offset_1 + stride,
                         in_data.begin() + offset_2);
    }
}

} // namespace Common
