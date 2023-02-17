// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common/thread_worker.h"

namespace VideoCore {

class SurfaceBase;

class HiresReplacer {
public:
    HiresReplacer();

    void DumpSurface(const SurfaceBase& surface, std::span<const u8> data);

private:
    Common::ThreadWorker workers;
    std::unordered_set<u64> dumped_surfaces;
};

} // namespace VideoCore
