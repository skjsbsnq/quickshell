#include "surface.hpp"

#include <qglobal.h>
#include <qhash.h>
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

/// Content equality by region id (order-independent). List order has no visual
/// semantics on the compositor — regions are stored/looked up by id.
bool sameRegionsById(
    const QList<TahoeGlassRegionState>& lhs,
    const QList<TahoeGlassRegionState>& rhs
) {
	if (lhs.size() != rhs.size()) return false;

	QHash<quint32, const TahoeGlassRegionState*> byId;
	byId.reserve(lhs.size());
	for (const auto& region: lhs) {
		byId.insert(region.id, &region);
	}

	for (const auto& region: rhs) {
		const auto* old = byId.value(region.id, nullptr);
		if (old == nullptr || !sameRegion(*old, region)) return false;
	}

	return true;
}

void emitSetRegion(QtWayland::tahoe_glass_surface_v1* surface, const TahoeGlassRegionState& region) {
	surface->set_region(
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

	// Order-independent no-op: pure reorder must not generate protocol traffic
	// or force a wl_surface commit.
	if (sameRegionsById(this->mRegions, regions)) {
		// Keep local list order in sync with the caller without wire traffic.
		this->mRegions = regions;
		return false;
	}

	// Full clear is a single protocol request when the new list is empty.
	if (regions.isEmpty()) {
		if (!this->mRegions.isEmpty()) {
			this->clear_regions();
			this->mRegions.clear();
			return true;
		}
		return false;
	}

	QHash<quint32, const TahoeGlassRegionState*> oldById;
	oldById.reserve(this->mRegions.size());
	for (const auto& region: this->mRegions) {
		oldById.insert(region.id, &region);
	}

	QHash<quint32, const TahoeGlassRegionState*> newById;
	newById.reserve(regions.size());
	for (const auto& region: regions) {
		newById.insert(region.id, &region);
	}

	bool changed = false;

	// Removals: ids present previously but absent now.
	for (const auto& old: this->mRegions) {
		if (!newById.contains(old.id)) {
			this->remove_region(old.id);
			changed = true;
		}
	}

	// Inserts and field updates: set_region replaces by id on the server.
	for (const auto& region: regions) {
		const auto* old = oldById.value(region.id, nullptr);
		if (old == nullptr || !sameRegion(*old, region)) {
			emitSetRegion(this, region);
			changed = true;
		}
	}

	this->mRegions = regions;
	return changed;
}

} // namespace qs::wayland::tahoe_glass::impl
