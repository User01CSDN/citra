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

namespace VideoCore {

struct StagingData;
class SurfaceParams;
enum class PixelFormat : u32;

enum class CustomFileFormat : u32 {
    PNG = 0,
    DDS = 1,
    KTX = 2,
};

struct CustomTexture {
    u32 width;
    u32 height;
    unsigned long long hash{};
    CustomPixelFormat format;
    CustomFileFormat file_format;
    std::string path;
    std::size_t staging_size;
    std::vector<u8> data;
    std::atomic_flag flag;
    bool decoded = false;

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

    /// Returns a unique indentifier for a 3DS texture
    u64 ComputeHash(const SurfaceParams& params, std::span<u8> data);

    /// Saves the provided pixel data described by params to disk as png
    void DumpTexture(const SurfaceParams& params, u32 level, std::span<u8> data);

    /// Returns the custom texture handle assigned to the provided data hash
    CustomTexture& GetTexture(u64 data_hash);

    /// Decodes the data in texture to a consumable format
    void DecodeToStaging(CustomTexture& texture, StagingData& staging);

    bool CompatibilityMode() const noexcept {
        return compatibility_mode;
    }

private:
    /// Fills the texture structure with information from the file in path
    void QueryTexture(CustomTexture& texture);

private:
    Core::System& system;
    std::unique_ptr<Common::ThreadWorker> workers;
    std::unordered_set<u64> dumped_textures;
    std::unordered_map<u64, CustomTexture*> custom_texture_map;
    std::vector<std::unique_ptr<CustomTexture>> custom_textures;
    std::vector<u8> temp_buffer;
    CustomTexture dummy_texture{};
    bool textures_loaded{};
    bool compatibility_mode{true};
};

} // namespace VideoCore
