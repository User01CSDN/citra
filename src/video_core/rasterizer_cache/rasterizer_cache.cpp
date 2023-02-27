// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/rasterizer_cache/rasterizer_cache.h"

namespace VideoCore {

MICROPROFILE_DEFINE(RasterizerCache_SurfaceCopy, "RasterizerCache", "Surface Copy",
                    MP_RGB(128, 192, 64));
MICROPROFILE_DEFINE(RasterizerCache_SurfaceLoad, "RasterizerCache", "Surface Load",
                    MP_RGB(128, 192, 64));
MICROPROFILE_DEFINE(RasterizerCache_SurfaceFlush, "RasterizerCache", "Surface Flush",
                    MP_RGB(128, 192, 64));
MICROPROFILE_DEFINE(RasterizerCache_Invalidation, "RasterizerCache", "Invalidation",
                    MP_RGB(128, 64, 192));
MICROPROFILE_DEFINE(RasterizerCache_Flush, "RasterizerCache", "Flush", MP_RGB(128, 64, 192));

} // namespace VideoCore
