// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "common/resource_pack/manifest.h"

namespace Common::ResourcePack {

class ResourcePack {
public:
    explicit ResourcePack(const std::string& path);

    bool IsValid() const {
        return m_valid;
    }

    const std::vector<char>& GetLogo() const {
        return m_logo_data;
    }

    const std::string& GetPath() const {
        return m_path;
    }

    const std::string& GetError() const {
        return m_error;
    }

    const Manifest& GetManifest() const {
        return m_manifest;
    }

    const std::vector<std::string>& GetTextures() const {
        return m_textures;
    }

    bool Install(const std::string& path);
    bool Uninstall(const std::string& path);

    bool operator==(const ResourcePack& pack) const;
    bool operator!=(const ResourcePack& pack) const;

private:
    bool m_valid = true;

    std::string m_path;
    std::string m_error;

    Manifest m_manifest;
    std::vector<std::string> m_textures;
    std::vector<char> m_logo_data;
};

} // namespace Common::ResourcePack
