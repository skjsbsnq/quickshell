#pragma once

#include <qlist.h>
#include <qrect.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtypes.h>
#include <qwayland-tahoe-glass-v1.h>

namespace qs::wayland::tahoe_glass::impl {

struct TahoeGlassCorners {
	qint32 topLeft = 0;
	qint32 topRight = 0;
	qint32 bottomRight = 0;
	qint32 bottomLeft = 0;
};

struct TahoeGlassRegionState {
	quint32 id = 0;
	QRect rect;
	TahoeGlassCorners corners;
	QString material = QStringLiteral("panel");
	quint32 flags = 0;
	qreal interaction = 0.0;
	qreal materialAlpha = 1.0;
};

class TahoeGlassSurface: public QtWayland::tahoe_glass_surface_v1 {
public:
	explicit TahoeGlassSurface(::tahoe_glass_surface_v1* surface);
	~TahoeGlassSurface() override;
	Q_DISABLE_COPY_MOVE(TahoeGlassSurface);

	[[nodiscard]] bool setRegions(const QList<TahoeGlassRegionState>& regions);

private:
	QList<TahoeGlassRegionState> mRegions;
};

} // namespace qs::wayland::tahoe_glass::impl
