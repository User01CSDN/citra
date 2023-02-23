// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/resource_pack/manifest.h"

namespace Common::ResourcePack {

template <typename T>
std::optional<T> GetOptional(const nlohmann::json& j, Manifest& manifest, std::string_view name) {
    try {
        return j.at(name).get<T>();
    } catch (const nlohmann::detail::out_of_range&) {
        LOG_DEBUG(Common, "Manifest {} does not state property: {}", manifest.name, name);
    }

    return std::nullopt;
}

void from_json(const nlohmann::json& j, Manifest& manifest) {
    try {
        manifest.name = j.at("name").get<std::string>();
        manifest.version = j.at("version").get<std::string>();
        manifest.id = j.at("id").get<std::string>();
        manifest.description = j.at("description").get<std::string>();
    } catch (const nlohmann::detail::out_of_range&) {
        manifest.error = "Some required fields are missing";
        manifest.valid = false;
        return;
    }

    manifest.website = GetOptional<std::string>(j, manifest, "website");
    manifest.authors = GetOptional<std::string>(j, manifest, "authors");
    const auto compressed = GetOptional<bool>(j, manifest, "compressed");
    if (compressed.has_value()) {
        manifest.compressed = compressed.value();
    }
}

} // namespace Common::ResourcePack
