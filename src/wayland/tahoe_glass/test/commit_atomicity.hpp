#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

namespace qs::wayland::tahoe_glass {
class TahoeGlass;
}

// Task 17: verifies the quickshell commit-deferral state machine that makes
// tahoe_glass region/transform state ride the scene-graph buffer commit
// atomically (one wl_surface commit per frame) instead of being committed
// early during polish against the previous buffer.
//
// The real Wayland surface is never exercised here: the in-flight flag is
// driven directly (or via a synthetic QEvent::UpdateRequest) and the explicit
// commit decision is observed through commitGlassIfIdle's test counter, which
// only counts when an explicit commit would actually be issued.
class TestCommitAtomicity: public QObject {
	Q_OBJECT;

private:
	// Friend helpers — must be members of TestCommitAtomicity (friend
	// declaration in qml.hpp gives this class access to TahoeGlass's
	// #ifdef QS_TEST seams and its private teardown overrides).
	static int commitsOf(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static bool inFlightOf(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void markInFlight(qs::wayland::tahoe_glass::TahoeGlass& glass, bool inFlight);
	static void deliverFrameSwapped(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void commitIfIdle(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void simulatePlatformSurfaceDestruction(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void simulateWaylandSurfaceDestruction(qs::wayland::tahoe_glass::TahoeGlass& glass);

private slots:
	void startsIdle();
	void idleCommitsExplicitly();
	void inFlightDefersCommit();
	void frameSwappedClearsInFlight();
	void updateRequestMarksInFlight();
	void fullCycleDeferThenCommit();
	void idempotentFrameSwapped();
	void teardownClearsInFlight();
};
