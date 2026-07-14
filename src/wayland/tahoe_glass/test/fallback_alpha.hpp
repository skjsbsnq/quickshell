#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qtmetamacros.h>

class PendingRegion;

namespace qs::wayland::tahoe_glass {
class TahoeGlass;
namespace impl {
struct TahoeGlassRegionState;
}
} // namespace qs::wayland::tahoe_glass

class TestFallbackAlpha: public QObject {
	Q_OBJECT;

private:
	// Friend helpers — must be members of TestFallbackAlpha.
	static PendingRegion* blurOf(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void apply(
	    qs::wayland::tahoe_glass::TahoeGlass& glass,
	    const QList<qs::wayland::tahoe_glass::impl::TahoeGlassRegionState>& regions
	);
	static void route(
	    qs::wayland::tahoe_glass::TahoeGlass& glass,
	    const QList<qs::wayland::tahoe_glass::impl::TahoeGlassRegionState>& regions,
	    bool protocolSurfacePresent
	);
	static QObject* effectObject(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static PendingRegion* fallbackRegion(qs::wayland::tahoe_glass::TahoeGlass& glass);

private slots:
	void alphaZeroClearsBlurRegion();
	void positiveAlphaEnablesBlur();
	void multipleRegionsAnyPositive();
	void reverseToExactZeroClearsSameUpdate();
	void protocolSurfacePresentClearsFallback();
	void protocolSurfaceGoneRebuildsFallback();
};
