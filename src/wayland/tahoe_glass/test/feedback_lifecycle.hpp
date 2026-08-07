#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

namespace qs::wayland::tahoe_glass {
class TahoeGlass;
}

class TestFeedbackLifecycle: public QObject {
	Q_OBJECT;

private:
	static quint32 begin(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void complete(qs::wayland::tahoe_glass::TahoeGlass& glass, quint32 serial, quint32 status);
	static void cancel(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void handoff(
	    qs::wayland::tahoe_glass::TahoeGlass& current,
	    qs::wayland::tahoe_glass::TahoeGlass& previous
	);
	static quint32 queueMorph(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static void publishMorph(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static bool hasPendingMorph(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static qsizetype serverOwnedCount(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static bool inFlight(qs::wayland::tahoe_glass::TahoeGlass& glass);
	static quint32 activeSerial(qs::wayland::tahoe_glass::TahoeGlass& glass);

private slots:
	void serialsAreMonotonicAndServerOwnsSentTerminals();
	void lateCompletionCannotFinishNewerSerial();
	void matchingCompletionClearsInFlightBeforeNotify();
	void teardownCancelsExactlyOnce();
	void activeFeedbackAndAllocatorTransferWithProtocolSurface();
	void unsentMorphIsCancelledInsteadOfTransferred();
	void newerRequestDropsUnsentMorphBeforeItCanReachServer();
	void serverActiveThenDeferredMorphKeepsRealTerminal();
	void teardownCancelsServerAndPendingSerials();
	void activeChangedHandlerCanInstallNewerRequest();
	void activeChangedHandlerCanTearDownTransaction();
	void terminalHandlerCanInstallNewerRequest();
	void handoffTransfersServerSerialButCancelsNewerPendingMorph();
};
