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
	if (!this->isActive()) return nullptr;
	return new TahoeGlassSurface(this->get_tahoe_glass_surface(window->surface()));
}

TahoeGlassManager* TahoeGlassManager::instance() {
	static auto* instance = new TahoeGlassManager(); // NOLINT
	return instance->isInitialized() ? instance : nullptr;
}

} // namespace qs::wayland::tahoe_glass::impl
