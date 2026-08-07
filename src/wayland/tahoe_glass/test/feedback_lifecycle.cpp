#include "feedback_lifecycle.hpp"

#include <qsignalspy.h>
#include <qtest.h>

#include "../../../window/proxywindow.hpp"
#include "../qml.hpp"

using qs::wayland::tahoe_glass::TahoeGlass;

quint32 TestFeedbackLifecycle::begin(TahoeGlass& glass) { return glass.beginFeedbackForTest(); }

void TestFeedbackLifecycle::complete(TahoeGlass& glass, quint32 serial, quint32 status) {
	glass.handleTransformFeedbackForTest(serial, status);
}

void TestFeedbackLifecycle::cancel(TahoeGlass& glass) { glass.cancelActiveFeedbackForTest(); }

void TestFeedbackLifecycle::handoff(TahoeGlass& current, TahoeGlass& previous) {
	current.handoffFeedbackStateForTest(previous);
}

quint32 TestFeedbackLifecycle::queueMorph(TahoeGlass& glass) {
	return glass.queuePendingMorphForTest();
}

void TestFeedbackLifecycle::publishMorph(TahoeGlass& glass) { glass.publishPendingMorphForTest(); }

bool TestFeedbackLifecycle::hasPendingMorph(TahoeGlass& glass) {
	return glass.hasPendingMorphForTest();
}

qsizetype TestFeedbackLifecycle::serverOwnedCount(TahoeGlass& glass) {
	return glass.serverOwnedCountForTest();
}

bool TestFeedbackLifecycle::inFlight(TahoeGlass& glass) { return glass.transformInFlight(); }

quint32 TestFeedbackLifecycle::activeSerial(TahoeGlass& glass) {
	return glass.activeTransformSerial();
}

void TestFeedbackLifecycle::serialsAreMonotonicAndServerOwnsSentTerminals() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);

	const auto first = begin(glass);
	const auto second = begin(glass);

	QVERIFY(first != 0);
	QVERIFY(second > first);
	QCOMPARE(activeSerial(glass), second);
	QVERIFY(inFlight(glass));
	QCOMPARE(serverOwnedCount(glass), qsizetype(2));
	QCOMPARE(finished.count(), 0);

	complete(glass, first, TahoeGlass::Superseded);
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.at(0).at(0).toUInt(), first);
	QCOMPARE(finished.at(0).at(1).toUInt(), quint32(TahoeGlass::Superseded));
}

void TestFeedbackLifecycle::lateCompletionCannotFinishNewerSerial() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	const auto first = begin(glass);
	const auto second = begin(glass);

	complete(glass, first, TahoeGlass::Completed);

	QCOMPARE(activeSerial(glass), second);
	QVERIFY(inFlight(glass));
}

void TestFeedbackLifecycle::matchingCompletionClearsInFlightBeforeNotify() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	const auto serial = begin(glass);
	bool inFlightAtNotify = true;
	QObject::connect(&glass, &TahoeGlass::transformFinished, &glass, [&](quint32, quint32) {
		inFlightAtNotify = glass.transformInFlight();
	});

	complete(glass, serial, TahoeGlass::Completed);

	QVERIFY(!inFlightAtNotify);
	QVERIFY(!inFlight(glass));
	QCOMPARE(activeSerial(glass), quint32(0));
}

void TestFeedbackLifecycle::teardownCancelsExactlyOnce() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);
	const auto serial = begin(glass);

	cancel(glass);
	cancel(glass);

	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.at(0).at(0).toUInt(), serial);
	QCOMPARE(finished.at(0).at(1).toUInt(), quint32(TahoeGlass::Cancelled));
	QVERIFY(!inFlight(glass));
}

void TestFeedbackLifecycle::activeFeedbackAndAllocatorTransferWithProtocolSurface() {
	ProxyWindowBase previousWindow;
	ProxyWindowBase currentWindow;
	TahoeGlass previous(&previousWindow);
	TahoeGlass current(&currentWindow);
	QSignalSpy previousFinished(&previous, &TahoeGlass::transformFinished);
	QSignalSpy currentFinished(&current, &TahoeGlass::transformFinished);
	QSignalSpy previousActiveChanged(&previous, &TahoeGlass::activeTransformSerialChanged);
	QSignalSpy currentActiveChanged(&current, &TahoeGlass::activeTransformSerialChanged);

	const auto transferred = begin(previous);
	handoff(current, previous);

	QCOMPARE(activeSerial(previous), quint32(0));
	QVERIFY(!inFlight(previous));
	QCOMPARE(activeSerial(current), transferred);
	QVERIFY(inFlight(current));
	QCOMPARE(previousFinished.count(), 0);
	QCOMPARE(currentFinished.count(), 0);
	QCOMPARE(previousActiveChanged.count(), 2); // begin + handoff
	QCOMPARE(currentActiveChanged.count(), 1);

	complete(current, transferred, TahoeGlass::Completed);
	QCOMPARE(currentFinished.count(), 1);
	QCOMPARE(currentFinished.at(0).at(0).toUInt(), transferred);
	QCOMPARE(currentFinished.at(0).at(1).toUInt(), quint32(TahoeGlass::Completed));
	QVERIFY(!inFlight(current));

	const auto next = begin(current);
	QCOMPARE(next, transferred + 1);
	complete(current, transferred, TahoeGlass::Completed);
	QCOMPARE(activeSerial(current), next);
	QVERIFY(inFlight(current));
}

void TestFeedbackLifecycle::unsentMorphIsCancelledInsteadOfTransferred() {
	ProxyWindowBase previousWindow;
	ProxyWindowBase currentWindow;
	TahoeGlass previous(&previousWindow);
	TahoeGlass current(&currentWindow);
	QSignalSpy previousFinished(&previous, &TahoeGlass::transformFinished);
	QSignalSpy currentFinished(&current, &TahoeGlass::transformFinished);

	const auto unsent = queueMorph(previous);
	QVERIFY(hasPendingMorph(previous));
	handoff(current, previous);

	QVERIFY(!inFlight(previous));
	QVERIFY(!hasPendingMorph(previous));
	QVERIFY(!inFlight(current));
	QCOMPARE(activeSerial(current), quint32(0));
	QCOMPARE(previousFinished.count(), 1);
	QCOMPARE(previousFinished.at(0).at(0).toUInt(), unsent);
	QCOMPARE(previousFinished.at(0).at(1).toUInt(), quint32(TahoeGlass::Cancelled));
	QCOMPARE(currentFinished.count(), 0);

	const auto next = begin(current);
	QVERIFY(next != unsent);
	QVERIFY(!hasPendingMorph(current));
}

void TestFeedbackLifecycle::newerRequestDropsUnsentMorphBeforeItCanReachServer() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);

	const auto morph = queueMorph(glass);
	QVERIFY(hasPendingMorph(glass));

	const auto replacement = begin(glass);
	QVERIFY(!hasPendingMorph(glass));
	QCOMPARE(activeSerial(glass), replacement);
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.at(0).at(0).toUInt(), morph);
	QCOMPARE(finished.at(0).at(1).toUInt(), quint32(TahoeGlass::Superseded));
}

void TestFeedbackLifecycle::serverActiveThenDeferredMorphKeepsRealTerminal() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);

	const auto serverActive = begin(glass);
	const auto pending = queueMorph(glass);
	QCOMPARE(activeSerial(glass), pending);
	QCOMPARE(serverOwnedCount(glass), qsizetype(1));
	QCOMPARE(finished.count(), 0);

	complete(glass, serverActive, TahoeGlass::Completed);
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.at(0).at(0).toUInt(), serverActive);
	QCOMPARE(finished.at(0).at(1).toUInt(), quint32(TahoeGlass::Completed));
	QCOMPARE(activeSerial(glass), pending);
	QVERIFY(inFlight(glass));

	publishMorph(glass);
	QVERIFY(!hasPendingMorph(glass));
	QCOMPARE(serverOwnedCount(glass), qsizetype(1));
	complete(glass, pending, TahoeGlass::Completed);
	QCOMPARE(finished.count(), 2);
	QVERIFY(!inFlight(glass));
}

void TestFeedbackLifecycle::teardownCancelsServerAndPendingSerials() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);
	const auto serverActive = begin(glass);
	const auto pending = queueMorph(glass);

	cancel(glass);
	cancel(glass);

	QCOMPARE(finished.count(), 2);
	QSet<quint32> cancelled;
	for (const auto& event: finished) {
		QCOMPARE(event.at(1).toUInt(), quint32(TahoeGlass::Cancelled));
		cancelled.insert(event.at(0).toUInt());
	}
	QCOMPARE(cancelled, QSet<quint32>({serverActive, pending}));
	QVERIFY(!inFlight(glass));
	QCOMPARE(serverOwnedCount(glass), qsizetype(0));
}

void TestFeedbackLifecycle::activeChangedHandlerCanInstallNewerRequest() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	quint32 nested = 0;
	bool reentered = false;
	QObject::connect(
	    &glass,
	    &TahoeGlass::activeTransformSerialChanged,
	    &glass,
	    [&]() {
		    if (reentered || activeSerial(glass) == 0) return;
		    reentered = true;
		    nested = begin(glass);
	    },
	    Qt::DirectConnection
	);

	const auto outer = begin(glass);
	QVERIFY(outer != 0);
	QVERIFY(nested != 0);
	QVERIFY(nested != outer);
	QCOMPARE(activeSerial(glass), nested);
	QCOMPARE(serverOwnedCount(glass), qsizetype(2));
}

void TestFeedbackLifecycle::activeChangedHandlerCanTearDownTransaction() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	QSignalSpy finished(&glass, &TahoeGlass::transformFinished);
	bool cancelled = false;
	QObject::connect(
	    &glass,
	    &TahoeGlass::activeTransformSerialChanged,
	    &glass,
	    [&]() {
		    if (cancelled || activeSerial(glass) == 0) return;
		    cancelled = true;
		    cancel(glass);
	    },
	    Qt::DirectConnection
	);

	const auto serial = begin(glass);
	QVERIFY(serial != 0);
	QVERIFY(cancelled);
	QVERIFY(!inFlight(glass));
	QCOMPARE(activeSerial(glass), quint32(0));
	QCOMPARE(serverOwnedCount(glass), qsizetype(0));
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.at(0).at(0).toUInt(), serial);
	QCOMPARE(finished.at(0).at(1).toUInt(), quint32(TahoeGlass::Cancelled));
}

void TestFeedbackLifecycle::terminalHandlerCanInstallNewerRequest() {
	ProxyWindowBase window;
	TahoeGlass glass(&window);
	const auto completed = begin(glass);
	quint32 nested = 0;
	QObject::connect(
	    &glass,
	    &TahoeGlass::transformFinished,
	    &glass,
	    [&](quint32 serial, quint32) {
		    if (serial == completed) nested = begin(glass);
	    },
	    Qt::DirectConnection
	);

	complete(glass, completed, TahoeGlass::Completed);
	QVERIFY(nested != 0);
	QCOMPARE(activeSerial(glass), nested);
	QVERIFY(inFlight(glass));
	QCOMPARE(serverOwnedCount(glass), qsizetype(1));
}

void TestFeedbackLifecycle::handoffTransfersServerSerialButCancelsNewerPendingMorph() {
	ProxyWindowBase previousWindow;
	ProxyWindowBase currentWindow;
	TahoeGlass previous(&previousWindow);
	TahoeGlass current(&currentWindow);
	QSignalSpy previousFinished(&previous, &TahoeGlass::transformFinished);
	QSignalSpy currentFinished(&current, &TahoeGlass::transformFinished);
	const auto serverActive = begin(previous);
	const auto pending = queueMorph(previous);

	handoff(current, previous);

	QCOMPARE(previousFinished.count(), 1);
	QCOMPARE(previousFinished.at(0).at(0).toUInt(), pending);
	QCOMPARE(previousFinished.at(0).at(1).toUInt(), quint32(TahoeGlass::Cancelled));
	QCOMPARE(serverOwnedCount(previous), qsizetype(0));
	QCOMPARE(serverOwnedCount(current), qsizetype(1));
	QCOMPARE(activeSerial(current), quint32(0));
	QVERIFY(!inFlight(current));

	complete(current, serverActive, TahoeGlass::Completed);
	QCOMPARE(currentFinished.count(), 1);
	QCOMPARE(currentFinished.at(0).at(0).toUInt(), serverActive);
	QCOMPARE(currentFinished.at(0).at(1).toUInt(), quint32(TahoeGlass::Completed));
}
