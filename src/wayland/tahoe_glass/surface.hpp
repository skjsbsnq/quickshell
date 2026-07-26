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

/// Protocol and local state use last-set-wins per region id.
/// Input lists may contain duplicate ids; the final occurrence wins and the
/// canonical list contains each id at most once (order of last occurrence).
[[nodiscard]] QList<TahoeGlassRegionState>
canonicalizeRegions(const QList<TahoeGlassRegionState>& regions);

struct TahoeGlassRegionDiff {
	bool changed = false;
	bool clearAll = false;
	QList<quint32> removeIds;
	QList<TahoeGlassRegionState> setRegions;
	/// Always canonical (unique ids, last-set-wins).
	QList<TahoeGlassRegionState> nextRegions;
};

/// Diff two region lists after canonicalizing both sides. Each id appears in
/// removeIds and setRegions at most once. Unchanged canonical content yields
/// changed=false and zero protocol ops (reorder-only is a no-op on the wire).
[[nodiscard]] TahoeGlassRegionDiff diffRegions(
    const QList<TahoeGlassRegionState>& oldRegions,
    const QList<TahoeGlassRegionState>& newRegions
);

class TahoeGlassSurface: public QtWayland::tahoe_glass_surface_v1 {
public:
	explicit TahoeGlassSurface(::tahoe_glass_surface_v1* surface);
	~TahoeGlassSurface() override;
	Q_DISABLE_COPY_MOVE(TahoeGlassSurface);

	[[nodiscard]] bool setRegions(const QList<TahoeGlassRegionState>& regions);

	/// Whether the compositor bound this surface at protocol version 4+,
	/// i.e. the presentation-transform requests below are usable.
	[[nodiscard]] bool supportsTransform() const;

	/// v4 presentation-transform requests. All are double-buffered server
	/// side (applied on the next wl_surface commit) and return false without
	/// sending anything when the bound version is below 4.
	bool setTransform(qreal x, qreal y, qreal scaleX, qreal scaleY);
	bool setTransformTargetSpring(
	    qreal x,
	    qreal y,
	    qreal scaleX,
	    qreal scaleY,
	    qreal dampingRatio,
	    qreal stiffness,
	    qreal epsilon
	);
	bool setTransformTargetEased(
	    qreal x,
	    qreal y,
	    qreal scaleX,
	    qreal scaleY,
	    qreal durationMs,
	    qreal x1,
	    qreal y1,
	    qreal x2,
	    qreal y2
	);
	bool
	setRegionMorphSpring(quint32 regionId, qreal dampingRatio, qreal stiffness, qreal epsilon);
	bool
	setRegionMorphEased(quint32 regionId, qreal durationMs, qreal x1, qreal y1, qreal x2, qreal y2);

private:
	bool ensureTransformSupported(const char* request) const;

	/// Canonical unique-id region state matching the last protocol content.
	QList<TahoeGlassRegionState> mRegions;
};

} // namespace qs::wayland::tahoe_glass::impl
