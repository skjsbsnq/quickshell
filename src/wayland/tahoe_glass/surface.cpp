#include "surface.hpp"
#include <utility>

#include <qdebug.h>
#include <qglobal.h>
#include <qhash.h>
#include <qlogging.h>
#include <qset.h>
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

/// Content equality by region id (order-independent). Both sides must already
/// be canonical (unique ids); list order has no visual semantics.
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

void emitSetRegion(
    QtWayland::tahoe_glass_surface_v1* surface,
    const TahoeGlassRegionState& region
) {
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

QList<TahoeGlassRegionState> canonicalizeRegions(const QList<TahoeGlassRegionState>& regions) {
	// Last-set-wins: walk reverse so the first time an id is seen is its final value.
	// Prepend restores left-to-right order of those last occurrences.
	QList<TahoeGlassRegionState> canonical;
	QSet<quint32> seen;
	canonical.reserve(regions.size());
	seen.reserve(regions.size());

	for (auto i = regions.size(); i > 0; --i) {
		const auto& region = regions.at(i - 1);
		if (seen.contains(region.id)) continue;
		seen.insert(region.id);
		canonical.prepend(region);
	}

	return canonical;
}

TahoeGlassRegionDiff diffRegions(
    const QList<TahoeGlassRegionState>& oldRegions,
    const QList<TahoeGlassRegionState>& newRegions
) {
	TahoeGlassRegionDiff diff;
	const auto oldCanonical = canonicalizeRegions(oldRegions);
	const auto newCanonical = canonicalizeRegions(newRegions);
	diff.nextRegions = newCanonical;

	// Order-independent no-op: pure reorder / duplicate expansion must not
	// generate protocol traffic.
	if (sameRegionsById(oldCanonical, newCanonical)) {
		return diff;
	}

	if (newCanonical.isEmpty()) {
		if (!oldCanonical.isEmpty()) {
			diff.changed = true;
			diff.clearAll = true;
		}
		return diff;
	}

	QHash<quint32, const TahoeGlassRegionState*> oldById;
	oldById.reserve(oldCanonical.size());
	for (const auto& region: oldCanonical) {
		oldById.insert(region.id, &region);
	}

	QHash<quint32, const TahoeGlassRegionState*> newById;
	newById.reserve(newCanonical.size());
	for (const auto& region: newCanonical) {
		newById.insert(region.id, &region);
	}

	// Removals: ids present previously but absent now (each id once).
	for (const auto& old: oldCanonical) {
		if (!newById.contains(old.id)) {
			diff.removeIds.append(old.id);
			diff.changed = true;
		}
	}

	// Inserts and field updates: set_region replaces by id on the server.
	// Iterate the canonical list so each id is set at most once.
	for (const auto& region: newCanonical) {
		const auto* old = oldById.value(region.id, nullptr);
		if (old == nullptr || !sameRegion(*old, region)) {
			diff.setRegions.append(region);
			diff.changed = true;
		}
	}

	return diff;
}

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

	const auto diff = diffRegions(this->mRegions, regions);

	// Keep local storage canonical even when the wire is a no-op (e.g. pure
	// reorder or a second set of the same duplicate-expanded list).
	if (!diff.changed) {
		this->mRegions = diff.nextRegions;
		return false;
	}

	// Full clear is a single protocol request when the new list is empty.
	// (Equivalent to diff.clearAll after canonicalize of an empty input.)
	if (regions.isEmpty()) {
		this->clear_regions();
		this->mRegions = diff.nextRegions;
		return true;
	}

	for (const auto id: diff.removeIds) {
		this->remove_region(id);
	}

	for (const auto& region: diff.setRegions) {
		emitSetRegion(this, region);
	}

	this->mRegions = diff.nextRegions;
	return true;
}

bool TahoeGlassSurface::supportsTransform() const {
	return this->isInitialized()
	    && this->QtWayland::tahoe_glass_surface_v1::version()
	           >= TAHOE_GLASS_SURFACE_V1_SET_TRANSFORM_SINCE_VERSION;
}

bool TahoeGlassSurface::supportsFeedback() const {
	return this->isInitialized()
	    && this->QtWayland::tahoe_glass_surface_v1::version()
	           >= TAHOE_GLASS_SURFACE_V1_SET_TRANSFORM_SERIAL_SINCE_VERSION;
}

bool TahoeGlassSurface::hasTransformFeedbackCapability(quint32 capabilities) {
	return (capabilities & capability_transform_feedback) != 0;
}

void TahoeGlassSurface::setEventHandlers(
    CapabilitiesHandler capabilities,
    FeedbackHandler feedback
) {
	this->mCapabilitiesHandler = std::move(capabilities);
	this->mFeedbackHandler = std::move(feedback);
}

void TahoeGlassSurface::tahoe_glass_surface_v1_capabilities(quint32 capabilities) {
	this->mCapabilities = capabilities;
	if (this->mCapabilitiesHandler) this->mCapabilitiesHandler(capabilities);
}

void TahoeGlassSurface::tahoe_glass_surface_v1_transform_feedback(quint32 serial, quint32 status) {
	if (this->mFeedbackHandler) this->mFeedbackHandler(serial, status);
}

bool TahoeGlassSurface::ensureTransformSupported(const char* request, quint32 serial) const {
	if (this->supportsTransform() && (serial == 0 || this->supportsFeedback())) return true;

	static bool warned = false;
	if (!warned) {
		warned = true;
		qWarning() << "tahoe_glass_surface_v1 request" << request << "needs protocol version"
		           << TAHOE_GLASS_SURFACE_V1_SET_TRANSFORM_SINCE_VERSION
		           << "- compositor bound an older version; presentation transforms disabled";
	}
	return false;
}

bool TahoeGlassSurface::setTransform(quint32 serial, qreal x, qreal y, qreal scaleX, qreal scaleY) {
	if (!this->ensureTransformSupported("set_transform", serial)) return false;

	if (serial != 0) this->set_transform_serial(serial);
	this->set_transform(
	    wl_fixed_from_double(x),
	    wl_fixed_from_double(y),
	    wl_fixed_from_double(scaleX),
	    wl_fixed_from_double(scaleY)
	);
	return true;
}

bool TahoeGlassSurface::setTransformTargetSpring(
    quint32 serial,
    qreal x,
    qreal y,
    qreal scaleX,
    qreal scaleY,
    qreal dampingRatio,
    qreal stiffness,
    qreal epsilon
) {
	if (!this->ensureTransformSupported("set_transform_target", serial)) return false;

	if (serial != 0) this->set_transform_serial(serial);
	this->set_transform_target(
	    wl_fixed_from_double(x),
	    wl_fixed_from_double(y),
	    wl_fixed_from_double(scaleX),
	    wl_fixed_from_double(scaleY),
	    transform_curve_spring,
	    wl_fixed_from_double(dampingRatio),
	    wl_fixed_from_double(stiffness),
	    wl_fixed_from_double(epsilon),
	    wl_fixed_from_double(0.0),
	    wl_fixed_from_double(0.0)
	);
	return true;
}

bool TahoeGlassSurface::setTransformTargetEased(
    quint32 serial,
    qreal x,
    qreal y,
    qreal scaleX,
    qreal scaleY,
    qreal durationMs,
    qreal x1,
    qreal y1,
    qreal x2,
    qreal y2
) {
	if (!this->ensureTransformSupported("set_transform_target", serial)) return false;

	if (serial != 0) this->set_transform_serial(serial);
	this->set_transform_target(
	    wl_fixed_from_double(x),
	    wl_fixed_from_double(y),
	    wl_fixed_from_double(scaleX),
	    wl_fixed_from_double(scaleY),
	    transform_curve_eased,
	    wl_fixed_from_double(durationMs),
	    wl_fixed_from_double(x1),
	    wl_fixed_from_double(y1),
	    wl_fixed_from_double(x2),
	    wl_fixed_from_double(y2)
	);
	return true;
}

bool TahoeGlassSurface::setRegionMorphSpring(
    quint32 serial,
    quint32 regionId,
    qreal dampingRatio,
    qreal stiffness,
    qreal epsilon
) {
	if (!this->ensureTransformSupported("set_region_morph", serial)) return false;

	if (serial != 0) this->set_transform_serial(serial);
	this->set_region_morph(
	    regionId,
	    transform_curve_spring,
	    wl_fixed_from_double(dampingRatio),
	    wl_fixed_from_double(stiffness),
	    wl_fixed_from_double(epsilon),
	    wl_fixed_from_double(0.0),
	    wl_fixed_from_double(0.0)
	);
	return true;
}

bool TahoeGlassSurface::setRegionMorphEased(
    quint32 serial,
    quint32 regionId,
    qreal durationMs,
    qreal x1,
    qreal y1,
    qreal x2,
    qreal y2
) {
	if (!this->ensureTransformSupported("set_region_morph", serial)) return false;

	if (serial != 0) this->set_transform_serial(serial);
	this->set_region_morph(
	    regionId,
	    transform_curve_eased,
	    wl_fixed_from_double(durationMs),
	    wl_fixed_from_double(x1),
	    wl_fixed_from_double(y1),
	    wl_fixed_from_double(x2),
	    wl_fixed_from_double(y2)
	);
	return true;
}

} // namespace qs::wayland::tahoe_glass::impl
