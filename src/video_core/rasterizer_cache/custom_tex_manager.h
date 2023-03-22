// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "common/thread_worker.h"
#include "video_core/rasterizer_cache/pixel_format.h"

namespace Core {
class System;
}

namespace Frontend {
class ImageInterface;
}

namespace VideoCore {

struct StagingData;
class SurfaceParams;
enum class PixelFormat : u32;

enum class CustomFileFormat : u32 {
    PNG = 0,
    DDS = 1,
    KTX = 2,
};

enum class DecodeState : u32 {
    None = 0,
    Pending = 1,
    Decoded = 2,
};

struct CustomTexture {
    u32 width;
    u32 height;
    unsigned long long hash{};
    CustomPixelFormat format;
    CustomFileFormat file_format;
    std::string path;
    std::vector<u8> data;
    DecodeState state{};

    operator bool() const noexcept {
        return hash != 0;
    }
};

class CustomTexManager {
public:
    CustomTexManager(Core::System& system);
    ~CustomTexManager();

    /// Searches the load directory assigned to program_id for any custom textures and loads them
    void FindCustomTextures();

    /// Preloads all registered custom textures
    void PreloadTextures();

    /// Returns a unique indentifier for a 3DS texture
    u64 ComputeHash(const SurfaceParams& params, std::span<u8> data);

    /// Saves the provided pixel data described by params to disk as png
    void DumpTexture(const SurfaceParams& params, u32 level, std::span<u8> data);

    /// Returns the custom texture handle assigned to the provided data hash
    CustomTexture& GetTexture(u64 data_hash);

private:
    /// Loads the texture from file decoding the data if needed
    void LoadTexture(CustomTexture& texture);

private:
    Core::System& system;
    Frontend::ImageInterface& image_interface;
    std::unique_ptr<Common::ThreadWorker> workers;
    std::unordered_set<u64> dumped_textures;
    std::unordered_map<u64, CustomTexture*> custom_texture_map;
    std::vector<std::unique_ptr<CustomTexture>> custom_textures;
    std::vector<u8> temp_buffer;
    CustomTexture dummy_texture{};
    bool textures_loaded{};
};

} // namespace VideoCore
