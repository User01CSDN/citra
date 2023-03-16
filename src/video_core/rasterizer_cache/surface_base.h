// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <list>
#include <memory>
#include <boost/icl/interval_set.hpp>
#include "common/assert.h"
#include "video_core/rasterizer_cache/surface_params.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"

namespace OpenGL {

class SurfaceBase;

/**
 * A watcher that notifies whether a cached surface has been changed. This is useful for caching
 * surface collection objects, including texture cube and mipmap.
 */
class SurfaceWatcher {
    friend class SurfaceBase;

public:
    explicit SurfaceWatcher(std::weak_ptr<SurfaceBase>&& surface) : surface(std::move(surface)) {}

    /// Checks whether the surface has been changed.
    bool IsValid() const {
        return !surface.expired() && valid;
    }

    /// Marks that the content of the referencing surface has been updated to the watcher user.
    void Validate() {
        ASSERT(!surface.expired());
        valid = true;
    }

    /// Gets the referencing surface. Returns null if the surface has been destroyed
    std::shared_ptr<SurfaceBase> Get() const {
        return surface.lock();
    }

private:
    std::weak_ptr<SurfaceBase> surface;
    bool valid = false;
};

using SurfaceRegions = boost::icl::interval_set<PAddr, std::less, SurfaceInterval>;

class SurfaceBase : public SurfaceParams, public std::enable_shared_from_this<SurfaceBase> {
public:
    SurfaceBase(const SurfaceParams& params);
    ~SurfaceBase();

    /// Returns true when this surface can be used to fill the fill_interval of dest_surface
    bool CanFill(const SurfaceParams& dest_surface, SurfaceInterval fill_interval) const;

    /// Returns true when surface can validate copy_interval of dest_surface
    bool CanCopy(const SurfaceParams& dest_surface, SurfaceInterval copy_interval) const;

    /// Returns the region of the biggest valid rectange within interval
    SurfaceInterval GetCopyableInterval(const SurfaceParams& params) const;

    std::shared_ptr<SurfaceWatcher> CreateWatcher();
    void InvalidateAllWatcher();
    void UnlinkAllWatcher();

    bool IsRegionValid(SurfaceInterval interval) const {
        return (invalid_regions.find(interval) == invalid_regions.end());
    }

    bool IsSurfaceFullyInvalid() const {
        auto interval = GetInterval();
        return *invalid_regions.equal_range(interval).first == interval;
    }

public:
    bool registered = false;
    SurfaceRegions invalid_regions;
    u32 fill_size = 0;
    std::array<u8, 4> fill_data;
    std::array<std::shared_ptr<SurfaceWatcher>, 7> level_watchers;
    std::list<std::weak_ptr<SurfaceWatcher>> watchers;
};

struct CachedTextureCube {
    OGLTexture texture;
    u16 res_scale = 1;
    std::shared_ptr<SurfaceWatcher> px;
    std::shared_ptr<SurfaceWatcher> nx;
    std::shared_ptr<SurfaceWatcher> py;
    std::shared_ptr<SurfaceWatcher> ny;
    std::shared_ptr<SurfaceWatcher> pz;
    std::shared_ptr<SurfaceWatcher> nz;
};

} // namespace OpenGL
