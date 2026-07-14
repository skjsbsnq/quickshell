#include "fallback_alpha.hpp"

#include <qguiapplication.h>
#include <qtest.h>

#include "../../../core/region.hpp"
#include "../../../window/proxywindow.hpp"
#include "../qml.hpp"
#include "../surface.hpp"

using qs::wayland::tahoe_glass::TahoeGlass;
using qs::wayland::tahoe_glass::impl::TahoeGlassRegionState;

namespace {

TahoeGlassRegionState
makeRegion(quint32 id, int x, int y, int w, int h, qreal materialAlpha, bool blur = true) {
	TahoeGlassRegionState s;
	s.id = id;
	s.rect = QRect(x, y, w, h);
	s.material = QStringLiteral("panel");
	s.flags = blur ? 1u : 0u;
	s.materialAlpha = materialAlpha;
	return s;
}

} // namespace

// Friend helpers must be members of TestFallbackAlpha (friend declaration).
PendingRegion* TestFallbackAlpha::blurOf(TahoeGlass& glass) {
	return glass.fallbackEffectBlurRegionForTest();
}

void TestFallbackAlpha::apply(TahoeGlass& glass, const QList<TahoeGlassRegionState>& regions) {
	glass.updateFallbackForTest(regions);
}

void TestFallbackAlpha::route(
    TahoeGlass& glass,
    const QList<TahoeGlassRegionState>& regions,
    bool protocolSurfacePresent
) {
	glass.routeRegionsAfterPolishForTest(regions, protocolSurfacePresent);
}

QObject* TestFallbackAlpha::effectObject(TahoeGlass& glass) {
	return glass.fallbackEffectObjectForTest();
}

PendingRegion* TestFallbackAlpha::fallbackRegion(TahoeGlass& glass) {
	return glass.fallbackRegionForTest();
}

void TestFallbackAlpha::alphaZeroClearsBlurRegion() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	apply(glass, {makeRegion(1, 0, 0, 40, 20, 1.0)});
	QVERIFY(blurOf(glass) != nullptr);

	apply(glass, {makeRegion(1, 0, 0, 40, 20, 0.0)});
	QCOMPARE(blurOf(glass), nullptr);
	QCOMPARE(fallbackRegion(glass), nullptr);
}

void TestFallbackAlpha::positiveAlphaEnablesBlur() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	apply(glass, {makeRegion(1, 4, 4, 32, 16, 0.02)});
	QVERIFY(blurOf(glass) != nullptr);

	apply(glass, {makeRegion(1, 4, 4, 32, 16, 0.5)});
	QVERIFY(blurOf(glass) != nullptr);

	apply(glass, {makeRegion(1, 4, 4, 32, 16, 1.0)});
	QVERIFY(blurOf(glass) != nullptr);
}

void TestFallbackAlpha::multipleRegionsAnyPositive() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	apply(
	    glass,
	    {
	        makeRegion(1, 0, 0, 10, 10, 0.0),
	        makeRegion(2, 10, 0, 10, 10, 0.5),
	    }
	);
	QVERIFY(blurOf(glass) != nullptr);

	apply(
	    glass,
	    {
	        makeRegion(1, 0, 0, 10, 10, 0.0),
	        makeRegion(2, 10, 0, 10, 10, 0.0),
	    }
	);
	QCOMPARE(blurOf(glass), nullptr);

	apply(glass, {makeRegion(3, 0, 0, 10, 10, 1.0, /*blur=*/false)});
	QCOMPARE(blurOf(glass), nullptr);
}

void TestFallbackAlpha::reverseToExactZeroClearsSameUpdate() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	apply(glass, {makeRegion(1, 0, 0, 20, 20, 1.0)});
	QVERIFY(blurOf(glass) != nullptr);

	apply(glass, {makeRegion(1, 0, 0, 20, 20, 0.0)});
	QCOMPARE(blurOf(glass), nullptr);
	QCOMPARE(fallbackRegion(glass), nullptr);
}

void TestFallbackAlpha::protocolSurfacePresentClearsFallback() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	// No protocol surface → fallback builds blur.
	route(glass, {makeRegion(1, 0, 0, 20, 20, 1.0)}, /*protocolSurfacePresent=*/false);
	QVERIFY(blurOf(glass) != nullptr);
	QCOMPARE(glass.available(), false);

	// Protocol surface appears → same polish routing clears fallback.
	route(glass, {makeRegion(1, 0, 0, 20, 20, 1.0)}, /*protocolSurfacePresent=*/true);
	QCOMPARE(blurOf(glass), nullptr);
	QCOMPARE(fallbackRegion(glass), nullptr);
	QCOMPARE(glass.available(), true);
}

void TestFallbackAlpha::protocolSurfaceGoneRebuildsFallback() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	glass.setFallbackEnabled(true);

	// Present first (clears any residual).
	route(glass, {makeRegion(1, 0, 0, 20, 20, 1.0)}, /*protocolSurfacePresent=*/true);
	QCOMPARE(blurOf(glass), nullptr);
	QCOMPARE(glass.available(), true);

	// Protocol surface gone → rebuild fallback from logical regions.
	route(glass, {makeRegion(1, 0, 0, 20, 20, 0.5)}, /*protocolSurfacePresent=*/false);
	QVERIFY(blurOf(glass) != nullptr);
	QCOMPARE(glass.available(), false);

	// No second blur strength/shader API on the effect sink.
	auto* effect = effectObject(glass);
	QVERIFY(effect != nullptr);
	QVERIFY(effect->metaObject()->indexOfProperty("blurStrength") < 0);
	QVERIFY(effect->metaObject()->indexOfProperty("shader") < 0);
}
