#include "manager.hpp"

#include <private/qwaylandwindow_p.h>
#include <qwayland-tahoe-glass-v1.h>
#include <qwaylandclientextension.h>

#include "surface.hpp"

namespace qs::wayland::tahoe_glass::impl {

TahoeGlassManager::TahoeGlassManager(): QWaylandClientExtensionTemplate(1) { this->initialize(); }

TahoeGlassSurface* TahoeGlassManager::createGlassSurface(QtWaylandClient::QWaylandWindow* window) {
	if (!this->isActive()) return nullptr;
	return new TahoeGlassSurface(this->get_tahoe_glass_surface(window->surface()));
}

TahoeGlassManager* TahoeGlassManager::instance() {
	static auto* instance = new TahoeGlassManager(); // NOLINT
	return instance->isInitialized() ? instance : nullptr;
}

} // namespace qs::wayland::tahoe_glass::impl
