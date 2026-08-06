#include "manager.hpp"

#include <private/qwaylandwindow_p.h>
#include <qwayland-tahoe-glass-v1.h>
#include <qwaylandclientextension.h>

#include "surface.hpp"

namespace qs::wayland::tahoe_glass::impl {

// Bind up to v4: v4 adds the presentation-transform requests
// (set_transform / set_transform_target / set_region_morph). Qt clamps to
// min(server version, 4); TahoeGlassSurface::supportsTransform() gates the
// v4 requests at runtime so an older niri keeps working with regions only.
TahoeGlassManager::TahoeGlassManager(): QWaylandClientExtensionTemplate(4) { this->initialize(); }

TahoeGlassSurface* TahoeGlassManager::createGlassSurface(QtWaylandClient::QWaylandWindow* window) {
	if (!this->isActive() || !window) return nullptr;
	return new TahoeGlassSurface(this->get_tahoe_glass_surface(window->surface()));
}

TahoeGlassManager* TahoeGlassManager::instance() {
	static auto* instance = new TahoeGlassManager(); // NOLINT
	// The generated isInitialized() flag only records that the global was
	// bound at least once; it says nothing about whether the binding is still
	// live. isActive() is the live-binding test: a compositor that never bound
	// the global (or a compositor restart that removed it) leaves the manager
	// inactive, and callers treat a null instance as "protocol unavailable"
	// so they stay on the fallback path (A08.4). createGlassSurface keeps its
	// own isActive() guard for direct callers.
	return instance->isActive() ? instance : nullptr;
}

} // namespace qs::wayland::tahoe_glass::impl
