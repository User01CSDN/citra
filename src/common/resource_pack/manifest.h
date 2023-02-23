// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <json.hpp>

namespace Common::ResourcePack {

struct Manifest {
    bool valid = true;
    bool compressed = false;
    std::string name;
    std::string version;
    std::string id;
    std::string error;
    std::string description;
    std::optional<std::string> authors;
    std::optional<std::string> website;
};

void to_json(nlohmann::json& j, const Manifest& manifest);

void from_json(const nlohmann::json& j, Manifest& manifest);

} // namespace Common::ResourcePack
