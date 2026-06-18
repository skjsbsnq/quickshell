#include "surface.hpp"

#include <qglobal.h>
#include <qwayland-tahoe-glass-v1.h>

namespace qs::wayland::tahoe_glass::impl {

namespace {

bool fuzzyEqual(qreal a, qreal b) { return qFuzzyCompare(1.0 + a, 1.0 + b); }

bool sameCorners(const TahoeGlassCorners& lhs, const TahoeGlassCorners& rhs) {
	return lhs.topLeft == rhs.topLeft && lhs.topRight == rhs.topRight
	    && lhs.bottomRight == rhs.bottomRight && lhs.bottomLeft == rhs.bottomLeft;
}

bool sameRegion(const TahoeGlassRegionState& lhs, const TahoeGlassRegionState& rhs) {
	return lhs.id == rhs.id && lhs.rect == rhs.rect && sameCorners(lhs.corners, rhs.corners)
	    && lhs.material == rhs.material && lhs.flags == rhs.flags
	    && fuzzyEqual(lhs.interaction, rhs.interaction)
	    && fuzzyEqual(lhs.materialAlpha, rhs.materialAlpha);
}

bool sameRegions(const QList<TahoeGlassRegionState>& lhs, const QList<TahoeGlassRegionState>& rhs) {
	if (lhs.size() != rhs.size()) return false;

	for (qsizetype i = 0; i < lhs.size(); i++) {
		if (!sameRegion(lhs.at(i), rhs.at(i))) return false;
	}

	return true;
}

} // namespace

TahoeGlassSurface::TahoeGlassSurface(
    ::tahoe_glass_surface_v1* surface // NOLINT(misc-include-cleaner)
)
    : QtWayland::tahoe_glass_surface_v1(surface) {}

TahoeGlassSurface::~TahoeGlassSurface() {
	if (!this->isInitialized()) return;
	this->destroy();
}

bool TahoeGlassSurface::setRegions(const QList<TahoeGlassRegionState>& regions) {
	if (!this->isInitialized()) return false;
	if (sameRegions(this->mRegions, regions)) return false;

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

	this->mRegions = regions;
	return true;
}

} // namespace qs::wayland::tahoe_glass::impl
