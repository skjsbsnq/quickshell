#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

namespace qs::wayland::tahoe_glass {
class TahoeGlass;
}

// Task 08: verifies the per-wl_surface mapping generation state machine on
// the TahoeGlass attached object.
//
// Scope note (honest): a real wl_surface / protocol surface cannot be created
// without a live Wayland connection, so the "protocol available" edge — the
// part of waylandSurfaceCreated that swaps in / creates a TahoeGlassSurface —
// is exercised here only through the advance seam, while the wiring that calls
// advanceMappingGeneration from waylandSurfaceCreated (and the setAvailable →
// advance ordering) is a source-structure contract enforced by the R17 shell
// test (test_dock_replays_compositor_slide_for_each_surface_mapping). The
// behavior tests below prove the generation semantics themselves: starts at
// zero, advances monotonically with one signal per advance, is untouched by
// ordinary commits and by surface teardown, and the protocol-unavailable path
// of waylandSurfaceCreated (the production code path taken when the compositor
// never binds the manager) is driven for real and must neither advance the
// generation nor crash.
class TestMappingLifecycle: public QObject {
	Q_OBJECT;

private:
	// Friend helpers — must be members of TestMappingLifecycle (friend
	// declaration in qml.hpp gives this class access to TahoeGlass's
	// #ifdef QS_TEST seams and private lifecycle overrides).
	static quint64 mappingGeneration(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void advanceMappingGeneration(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void simulateSurfaceCreated(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void simulateSurfaceDestroyed(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static bool available(qs::wayland::tahoe_glass::TahoeGlass& glass);

private slots:
	void startsAtZero();
	void advanceAdvancesGeneration();
	void advanceIsMonotonicAcrossMultiple();
	void advanceEmitsOneSignal();
	void notifyObservesNewValue();
	void ordinaryCommitsDoNotAdvanceGeneration();
	void surfaceDestroyedDoesNotAdvanceGeneration();
	void protocolUnavailableDoesNotAdvanceGeneration();
};
