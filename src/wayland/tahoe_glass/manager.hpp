#pragma once

#include <private/qwaylandwindow_p.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qwayland-tahoe-glass-v1.h>
#include <qwaylandclientextension.h>

#include "surface.hpp"

namespace qs::wayland::tahoe_glass::impl {

class TahoeGlassManager
    : public QWaylandClientExtensionTemplate<TahoeGlassManager>
    , public QtWayland::tahoe_glass_manager_v1 {
	Q_OBJECT;

public:
	explicit TahoeGlassManager();

	TahoeGlassSurface* createGlassSurface(QtWaylandClient::QWaylandWindow* window);

	static TahoeGlassManager* instance();
};

} // namespace qs::wayland::tahoe_glass::impl
