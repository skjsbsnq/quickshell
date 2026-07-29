#include "commit_atomicity.hpp"

#include <qcoreevent.h>
#include <qguiapplication.h>
#include <qobject.h>
#include <qtest.h>

#include "../../../window/proxywindow.hpp"
#include "../qml.hpp"

using qs::wayland::tahoe_glass::TahoeGlass;

// Friend helpers must be members of TestCommitAtomicity (friend declaration).
int TestCommitAtomicity::commitsOf(TahoeGlass& glass) {
	return glass.explicitCommitCountForTest();
}

bool TestCommitAtomicity::inFlightOf(TahoeGlass& glass) {
	return glass.repaintInFlightForTest();
}

void TestCommitAtomicity::markInFlight(TahoeGlass& glass, bool inFlight) {
	glass.setRepaintInFlightForTest(inFlight);
}

void TestCommitAtomicity::deliverFrameSwapped(TahoeGlass& glass) {
	glass.emitFrameSwappedForTest();
}

void TestCommitAtomicity::commitIfIdle(TahoeGlass& glass) {
	glass.commitGlassIfIdleForTest();
}

void TestCommitAtomicity::simulatePlatformSurfaceDestruction(TahoeGlass& glass) {
	glass.platformSurfaceAboutToBeDestroyed();
}

void TestCommitAtomicity::simulateWaylandSurfaceDestruction(TahoeGlass& glass) {
	glass.waylandSurfaceDestroyed();
}

namespace {
// Drive the UpdateRequest branch of filteredWindowEvent without a live
// scene graph. eventFilter is public (QObject override); the UpdateRequest
// branch only sets the in-flight flag (pendingRegions is false here, so
// updateRegions is not re-armed) and is independent of the watched object.
void deliverUpdateRequest(TahoeGlass& glass, QObject* watched) {
	QEvent updateRequest(QEvent::UpdateRequest);
	glass.eventFilter(watched, &updateRequest);
}
} // namespace

void TestCommitAtomicity::startsIdle() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	QCOMPARE(inFlightOf(glass), false);
	QCOMPARE(commitsOf(glass), 0);
}

void TestCommitAtomicity::idleCommitsExplicitly() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// No render in flight → an explicit commit is the only thing that can
	// carry pending region/transform state, so one must be issued.
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 1);

	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 2);
}

void TestCommitAtomicity::inFlightDefersCommit() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	markInFlight(glass, true);
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 0); // deferred to the render-thread buffer commit

	markInFlight(glass, true);
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 0); // still deferred while in flight
}

void TestCommitAtomicity::frameSwappedClearsInFlight() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	markInFlight(glass, true);
	deliverFrameSwapped(glass);
	QCOMPARE(inFlightOf(glass), false);

	// Once the frame's buffer commit has carried the state, idle commits resume.
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 1);
}

void TestCommitAtomicity::updateRequestMarksInFlight() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	deliverUpdateRequest(glass, &window);
	QCOMPARE(inFlightOf(glass), true);

	// A render cycle started: the scene graph will commit a buffer this frame,
	// so the explicit commit is suppressed to keep region/transform atomic
	// with the new content.
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 0);
}

void TestCommitAtomicity::fullCycleDeferThenCommit() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// Frame begins (UpdateRequest): defer.
	deliverUpdateRequest(glass, &window);
	QVERIFY(inFlightOf(glass));
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 0);

	// Frame ends (frameSwapped): the buffer commit carried the state.
	deliverFrameSwapped(glass);
	QVERIFY(!inFlightOf(glass));

	// A later idle update (no repaint queued) commits explicitly again —
	// this is the path that keeps pure metadata/transform updates from
	// sticking in pending state forever.
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 1);
}

void TestCommitAtomicity::idempotentFrameSwapped() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// frameSwapped when already idle is a no-op (e.g. frames with no glass
	// state); it must not flip the flag into an invalid state.
	deliverFrameSwapped(glass);
	QCOMPARE(inFlightOf(glass), false);
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 1);

	// Repeated clears are harmless.
	markInFlight(glass, true);
	deliverFrameSwapped(glass);
	deliverFrameSwapped(glass);
	QCOMPARE(inFlightOf(glass), false);
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 2);
}

void TestCommitAtomicity::teardownClearsInFlight() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);

	// Both surface-teardown hooks reset the in-flight flag so a stuck flag
	// can never outlive the surface (e.g. a render aborted between
	// UpdateRequest and frameSwapped). platformSurfaceAboutToBeDestroyed
	// fires on platform surface teardown; waylandSurfaceDestroyed on the
	// wl_surface going away (the normal hide path).
	markInFlight(glass, true);
	simulatePlatformSurfaceDestruction(glass);
	QVERIFY(!inFlightOf(glass));

	markInFlight(glass, true);
	simulateWaylandSurfaceDestruction(glass);
	QVERIFY(!inFlightOf(glass));

	// After teardown the flag is clean, so a subsequent idle commit still
	// proceeds (no permanent deferral across the surface lifetime).
	commitIfIdle(glass);
	QCOMPARE(commitsOf(glass), 1);
}
