// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <inih/cpp/INIReader.h>
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/resource_pack/manager.h"

namespace Common::ResourcePack {

Manager::Manager() {
    packs_path = FileUtil::GetUserPath(FileUtil::UserPath::ResourcePackDir);
    reader = std::make_unique<INIReader>(packs_path);

    auto pack_list = FileUtil::DoFileSearch({packs_path}, {".zip"});

    auto* order = file.GetOrCreateSection("Order");

    struct OrderHelper {
        size_t pack_list_index;
        std::string manifest_id;
    };

    std::vector<OrderHelper> pack_list_order;
    pack_list_order.reserve(pack_list.size());
    for (size_t i = 0; i < pack_list.size(); ++i) {
        const std::string pack_path = pack_list[i].physicalName;
        const ResourcePack pack(pack_path);
        std::string manifest_id = pack.IsValid() ? pack.GetManifest().id : pack_path;
        pack_list_order.emplace_back(OrderHelper{i, std::move(manifest_id)});
    }

    std::sort(
        pack_list_order.begin(), pack_list_order.end(),
        [](const OrderHelper& a, const OrderHelper& b) { return a.manifest_id < b.manifest_id; });

    bool error = false;
    for (size_t i = 0; i < pack_list_order.size(); ++i) {
        const auto& path = pack_list[pack_list_order[i].pack_list_index].physicalName;

        const ResourcePack* const pack = Add(path);
        if (pack == nullptr) {
            error = true;
            continue;
        }

        order->Set(pack->GetManifest().id, static_cast<u64>(i));
    }

    file.Save(packs_path);
}

ResourcePack* Manager::Add(const std::string& path, int offset) {
    if (offset == -1)
        offset = static_cast<int>(packs.size());

    ResourcePack pack(path);

    if (!pack.IsValid())
        return nullptr;

    IniFile file = GetPackConfig();

    auto* order = file.GetOrCreateSection("Order");

    order->Set(pack.GetManifest()->GetID(), offset);

    for (int i = offset; i < static_cast<int>(packs.size()); i++)
        order->Set(packs[i].GetManifest()->GetID(), i + 1);

    file.Save(packs_path);

    auto it = packs.insert(packs.begin() + offset, std::move(pack));
    return &*it;
}

bool Manager::Remove(ResourcePack& pack) {
    const auto result = pack.Uninstall(File::GetUserPath(D_USER_IDX));

    if (!result)
        return false;

    auto pack_iterator = std::find(packs.begin(), packs.end(), pack);

    if (pack_iterator == packs.end())
        return false;

    IniFile file = GetPackConfig();

    auto* order = file.GetOrCreateSection("Order");

    order->Delete(pack.GetManifest().id);

    int offset = pack_iterator - packs.begin();

    for (int i = offset + 1; i < static_cast<int>(packs.size()); i++)
        order->Set(packs[i].GetManifest().id, i - 1);

    file.Save(packs_path);

    packs.erase(pack_iterator);

    return true;
}

void Manager::SetInstalled(const ResourcePack& pack, bool installed) {
    IniFile file = GetPackConfig();

    auto* install = file.GetOrCreateSection("Installed");

    if (installed)
        install->Set(pack.GetManifest().id, installed);
    else
        install->Delete(pack.GetManifest().id);

    file.Save(packs_path);
}

bool Manager::IsInstalled(const ResourcePack& pack) {
    IniFile file = GetPackConfig();

    auto* install = file.GetOrCreateSection("Installed");

    bool installed;

    install->Get(pack.GetManifest().id, &installed, false);

    return installed;
}

std::vector<ResourcePack*> Manager::GetLowerPriorityPacks(ResourcePack& pack) {
    std::vector<ResourcePack*> list;
    for (auto it = std::find(packs.begin(), packs.end(), pack) + 1; it != packs.end(); ++it) {
        auto& entry = *it;
        if (!IsInstalled(pack)) {
            continue;
        }

        list.push_back(&entry);
    }

    return list;
}

std::vector<ResourcePack*> Manager::GetHigherPriorityPacks(ResourcePack& pack) {
    std::vector<ResourcePack*> list;
    auto end = std::find(packs.begin(), packs.end(), pack);

    for (auto it = packs.begin(); it != end; ++it) {
        auto& entry = *it;
        if (!IsInstalled(entry)) {
            continue;
        }
        list.push_back(&entry);
    }

    return list;
}

} // namespace Common::ResourcePack
