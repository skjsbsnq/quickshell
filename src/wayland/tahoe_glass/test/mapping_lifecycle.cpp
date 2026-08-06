#include "mapping_lifecycle.hpp"

#include <qsignalspy.h>
#include <qtest.h>

#include "../../../window/proxywindow.hpp"
#include "../qml.hpp"

using qs::wayland::tahoe_glass::TahoeGlass;

// Friend helpers must be members of TestMappingLifecycle (friend declaration).
quint64 TestMappingLifecycle::mappingGeneration(TahoeGlass& glass) {
	return glass.mappingGeneration();
}

void TestMappingLifecycle::advanceMappingGeneration(TahoeGlass& glass) {
	glass.advanceMappingGenerationForTest();
}

void TestMappingLifecycle::simulateSurfaceCreated(TahoeGlass& glass) {
	glass.waylandSurfaceCreated();
}

void TestMappingLifecycle::simulateSurfaceDestroyed(TahoeGlass& glass) {
	glass.waylandSurfaceDestroyed();
}

bool TestMappingLifecycle::available(TahoeGlass& glass) {
	return glass.available();
}

void TestMappingLifecycle::startsAtZero() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	QCOMPARE(mappingGeneration(glass), quint64(0));
}

void TestMappingLifecycle::advanceAdvancesGeneration() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	const auto before = mappingGeneration(glass);
	advanceMappingGeneration(glass);

	QVERIFY(mappingGeneration(glass) > before);
}

void TestMappingLifecycle::advanceIsMonotonicAcrossMultiple() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	const auto first = mappingGeneration(glass);
	advanceMappingGeneration(glass);
	const auto second = mappingGeneration(glass);
	advanceMappingGeneration(glass);
	const auto third = mappingGeneration(glass);

	QVERIFY(second > first);
	QVERIFY(third > second);
}

void TestMappingLifecycle::advanceEmitsOneSignal() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	QSignalSpy spy(&glass, &TahoeGlass::mappingGenerationChanged);

	advanceMappingGeneration(glass);
	advanceMappingGeneration(glass);

	QCOMPARE(spy.count(), 2);
}

void TestMappingLifecycle::notifyObservesNewValue() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// Handlers keyed on the generation must observe the new value at notify
	// time (the increment happens before the signal is emitted).
	quint64 generationAtNotify = 0;
	QObject::connect(&glass, &TahoeGlass::mappingGenerationChanged, &glass, [&]() {
		generationAtNotify = glass.mappingGeneration();
	});

	advanceMappingGeneration(glass);

	QCOMPARE(generationAtNotify, mappingGeneration(glass));
	QVERIFY(generationAtNotify > 0);
}

void TestMappingLifecycle::ordinaryCommitsDoNotAdvanceGeneration() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	advanceMappingGeneration(glass);
	const auto afterFirst = mappingGeneration(glass);

	// Ordinary region/transform commits are not wl_surface mapping lifecycle
	// events: the generation must be stable across them (A08.2).
	glass.updateRegionsForTest();
	glass.sendTransform(0, 0, 1, 1);
	const auto afterCommits = mappingGeneration(glass);
	QCOMPARE(afterCommits, afterFirst);
}

void TestMappingLifecycle::surfaceDestroyedDoesNotAdvanceGeneration() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	advanceMappingGeneration(glass);
	const auto afterFirst = mappingGeneration(glass);

	simulateSurfaceDestroyed(glass);
	QCOMPARE(mappingGeneration(glass), afterFirst);
}

void TestMappingLifecycle::protocolUnavailableDoesNotAdvanceGeneration() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// No Wayland connection in this harness: the compositor never bound the
	// manager, so the protocol-unavailable branch of waylandSurfaceCreated is
	// the production path taken on real systems that lack the protocol. It
	// must not crash, must not produce a protocol surface, and must not
	// advance the mapping generation (there is no mapping to replay a
	// compositor slide on — A08.4).
	const auto before = mappingGeneration(glass);

	simulateSurfaceCreated(glass);

	QCOMPARE(mappingGeneration(glass), before);
	QCOMPARE(available(glass), false);
}
