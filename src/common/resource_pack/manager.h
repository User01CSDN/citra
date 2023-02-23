// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <vector>
#include "common/resource_pack/resource_pack.h"

class INIReader;

namespace Common::ResourcePack {

class Manager {
public:
    Manager();

    ResourcePack* Add(const std::string& path, int offset = -1);
    bool Remove(ResourcePack& pack);
    void SetInstalled(const ResourcePack& pack, bool installed);
    bool IsInstalled(const ResourcePack& pack);

    std::span<ResourcePack> GetPacks() {
        return packs;
    }

    std::vector<ResourcePack*> GetHigherPriorityPacks(ResourcePack& pack);
    std::vector<ResourcePack*> GetLowerPriorityPacks(ResourcePack& pack);

private:
    std::unique_ptr<INIReader> reader;
    std::vector<ResourcePack> packs;
    std::string packs_path;
};

bool Init();

ResourcePack* Add(const std::string& path, int offset = -1);
bool Remove(ResourcePack& pack);
void SetInstalled(const ResourcePack& pack, bool installed);
bool IsInstalled(const ResourcePack& pack);

std::vector<ResourcePack>& GetPacks();

std::vector<ResourcePack*> GetHigherPriorityPacks(ResourcePack& pack);
std::vector<ResourcePack*> GetLowerPriorityPacks(ResourcePack& pack);

} // namespace Common::ResourcePack
