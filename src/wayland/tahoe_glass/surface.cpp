#include "surface.hpp"

#include <qwayland-tahoe-glass-v1.h>

namespace qs::wayland::tahoe_glass::impl {

TahoeGlassSurface::TahoeGlassSurface(
    ::tahoe_glass_surface_v1* surface // NOLINT(misc-include-cleaner)
)
    : QtWayland::tahoe_glass_surface_v1(surface) {}

TahoeGlassSurface::~TahoeGlassSurface() {
	if (!this->isInitialized()) return;
	this->destroy();
}

void TahoeGlassSurface::setRegions(const QList<TahoeGlassRegionState>& regions) {
	if (!this->isInitialized()) return;

	this->clear_regions();

	for (const auto& region: regions) {
		this->set_region(
		    region.id,
		    region.rect.x(),
		    region.rect.y(),
		    region.rect.width(),
		    region.rect.height(),
		    region.corners.topLeft,
		    region.corners.topRight,
		    region.corners.bottomRight,
		    region.corners.bottomLeft,
			    region.material,
			    region.flags,
			    wl_fixed_from_double(region.interaction),
			    wl_fixed_from_double(region.materialAlpha)
			);

	}
}

} // namespace qs::wayland::tahoe_glass::impl
