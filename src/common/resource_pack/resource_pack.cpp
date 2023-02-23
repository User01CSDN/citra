// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <limits>
#include <memory>
#include <json.hpp>
#include <mz_compat.h>
#include <mz_os.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/minizip_util.h"
#include "common/resource_pack/manager.h"
#include "common/resource_pack/manifest.h"
#include "common/resource_pack/resource_pack.h"
#include "common/string_util.h"

namespace Common::ResourcePack {

constexpr char TEXTURE_PATH[] = LOAD_DIR DIR_SEP;

ResourcePack::ResourcePack(const std::string& path) : m_path(path) {
    unzFile file = unzOpen(path.c_str());
    SCOPE_EXIT({ unzClose(file); });

    if (!file) {
        m_valid = false;
        m_error = "Failed to open resource pack";
        return;
    }

    if (unzLocateFile(file, "manifest.json", 0) == UNZ_END_OF_LIST_OF_FILE) {
        m_valid = false;
        m_error = "Resource pack is missing a manifest.";
        return;
    }

    unz_file_info64 manifest_info{};
    unzGetCurrentFileInfo64(file, &manifest_info, nullptr, 0, nullptr, 0, nullptr, 0);

    std::string manifest_contents(manifest_info.uncompressed_size, '\0');
    if (!Common::ReadFileFromZip(file, &manifest_contents)) {
        m_valid = false;
        m_error = "Failed to read manifest.json";
        return;
    }
    unzCloseCurrentFile(file);

    auto manifest_json = nlohmann::json::parse(manifest_contents);
    m_manifest = manifest_json.get<Manifest>();

    if (!m_manifest.valid) {
        m_valid = false;
        m_error = "Manifest error: " + m_manifest.error;
        return;
    }

    if (unzLocateFile(file, "logo.png", 0) != UNZ_END_OF_LIST_OF_FILE) {
        unz_file_info64 logo_info{};
        unzGetCurrentFileInfo64(file, &logo_info, nullptr, 0, nullptr, 0, nullptr, 0);

        m_logo_data.resize(logo_info.uncompressed_size);

        if (!Common::ReadFileFromZip(file, &m_logo_data)) {
            m_valid = false;
            m_error = "Failed to read logo.png";
            return;
        }
    }

    unzGoToFirstFile(file);

    do {
        std::string filename(256, '\0');

        unz_file_info64 texture_info{};
        unzGetCurrentFileInfo64(file, &texture_info, filename.data(),
                                static_cast<u16>(filename.size()), nullptr, 0, nullptr, 0);

        if (filename.compare(0, 9, "textures/") != 0 || texture_info.uncompressed_size == 0)
            continue;

        // If a texture is compressed and the manifest doesn't state that, abort.
        if (!m_manifest.compressed && texture_info.compression_method != 0) {
            m_valid = false;
            m_error = "Texture " + filename + " is compressed!";
            return;
        }

        m_textures.push_back(filename.substr(9));
    } while (unzGoToNextFile(file) != UNZ_END_OF_LIST_OF_FILE);
}

bool ResourcePack::Install(const std::string& path) {
    if (!IsValid()) {
        m_error = "Invalid pack";
        return false;
    }

    unzFile file = unzOpen(m_path.c_str());
    if (!file) {
        m_valid = false;
        m_error = "Failed to open resource pack";
        return false;
    }
    SCOPE_EXIT({ unzClose(file); });

    if (unzGoToFirstFile(file) != MZ_OK) {
        return false;
    }

    std::string texture_zip_path;
    do {
        texture_zip_path.resize(std::numeric_limits<u16>::max() + 1, '\0');
        unz_file_info64 texture_info{};
        if (unzGetCurrentFileInfo64(file, &texture_info, texture_zip_path.data(), UINT16_MAX,
                                    nullptr, 0, nullptr, 0) != MZ_OK) {
            return false;
        }
        TruncateToCString(&texture_zip_path);

        const std::string texture_zip_path_prefix = "textures/";
        if (!texture_zip_path.starts_with(texture_zip_path_prefix)) {
            continue;
        }
        const std::string texture_name = texture_zip_path.substr(texture_zip_path_prefix.size());

        auto texture_it = std::find_if(
            m_textures.cbegin(), m_textures.cend(), [&texture_name](const std::string& texture) {
                return mz_path_compare_wc(texture.c_str(), texture_name.c_str(), 1) == MZ_OK;
            });
        if (texture_it == m_textures.cend())
            continue;
        const auto texture = *texture_it;

        // Check if a higher priority pack already provides a given texture, don't overwrite it
        bool provided_by_other_pack = false;
        for (const auto& pack : GetHigherPriorityPacks(*this)) {
            if (std::find(pack->GetTextures().begin(), pack->GetTextures().end(), texture) !=
                pack->GetTextures().end()) {
                provided_by_other_pack = true;
                break;
            }
        }
        if (provided_by_other_pack) {
            continue;
        }

        const std::string texture_path = path + TEXTURE_PATH + texture;
        std::string texture_full_dir;
        if (!SplitPath(texture_path, &texture_full_dir, nullptr, nullptr)) {
            continue;
        }

        if (!FileUtil::CreateFullPath(texture_full_dir)) {
            m_error = "Failed to create full path " + texture_full_dir;
            return false;
        }

        const size_t data_size = static_cast<size_t>(texture_info.uncompressed_size);
        auto data = std::make_unique<u8[]>(data_size);
        if (!Common::ReadFileFromZip(file, data.get(), data_size)) {
            m_error = "Failed to read texture " + texture;
            return false;
        }

        FileUtil::IOFile out(texture_path, "wb");
        if (!out) {
            m_error = "Failed to open " + texture;
            return false;
        }
        if (!out.WriteBytes(data.get(), data_size)) {
            m_error = "Failed to write " + texture;
            return false;
        }
    } while (unzGoToNextFile(file) == MZ_OK);

    SetInstalled(*this, true);
    return true;
}

bool ResourcePack::Uninstall(const std::string& path) {
    if (!IsValid()) {
        m_error = "Invalid pack";
        return false;
    }

    auto lower = GetLowerPriorityPacks(*this);

    SetInstalled(*this, false);

    for (const auto& texture : m_textures) {
        bool provided_by_other_pack = false;

        // Check if a higher priority pack already provides a given texture, don't delete it
        for (const auto& pack : GetHigherPriorityPacks(*this)) {
            const std::vector<std::string>& textures = pack->GetTextures();
            if (IsInstalled(*pack) &&
                std::find(textures.begin(), textures.end(), texture) != textures.end()) {
                provided_by_other_pack = true;
                break;
            }
        }

        if (provided_by_other_pack)
            continue;

        // Check if a lower priority pack provides a given texture - if so, install it.
        for (auto& pack : lower) {
            const std::vector<std::string>& textures = pack->GetTextures();
            if (IsInstalled(*pack) &&
                std::find(textures.rbegin(), textures.rend(), texture) != textures.rend()) {
                pack->Install(path);

                provided_by_other_pack = true;
                break;
            }
        }

        if (provided_by_other_pack)
            continue;

        const std::string texture_path = path + TEXTURE_PATH + texture;
        if (FileUtil::Exists(texture_path) && !FileUtil::Delete(texture_path)) {
            m_error = "Failed to delete texture " + texture;
            return false;
        }

        // Recursively delete empty directories
        std::string dir;
        SplitPath(texture_path, &dir, nullptr, nullptr);

        while (dir.length() > (path + TEXTURE_PATH).length()) {
            auto is_empty = FileUtil::DoFileSearch({dir}).empty();

            if (is_empty) {
                FileUtil::DeleteDir(dir);
            }

            SplitPath(dir.substr(0, dir.size() - 2), &dir, nullptr, nullptr);
        }
    }

    return true;
}

bool ResourcePack::operator==(const ResourcePack& pack) const {
    return pack.GetPath() == m_path;
}

bool ResourcePack::operator!=(const ResourcePack& pack) const {
    return !operator==(pack);
}

} // namespace Common::ResourcePack
