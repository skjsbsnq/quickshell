#include "systemclock.hpp"

#include <qdatetime.h>
#include <qmetaobject.h>
#include <qsignalspy.h>
#include <qtest.h>
#include <qtimezone.h>

#include "../clock.hpp"

namespace {

QDateTime makeLocal(int year, int month, int day, int hour, int minute, int second = 0) {
	return QDateTime(QDate(year, month, day), QTime(hour, minute, second));
}

} // namespace

void TestSystemClock::initialValue() {
	auto now = makeLocal(2026, 7, 14, 10, 15, 30);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Seconds);
	clock.resync();

	QCOMPARE(clock.hours(), quint32(10));
	QCOMPARE(clock.minutes(), quint32(15));
	QCOMPARE(clock.seconds(), quint32(30));
	QCOMPARE(clock.date().time().hour(), 10);
	QCOMPARE(clock.date().time().minute(), 15);
	QCOMPARE(clock.date().time().second(), 30);
}

void TestSystemClock::explicitResync() {
	auto now = makeLocal(2026, 7, 14, 10, 15, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.resync();
	QCOMPARE(clock.minutes(), quint32(15));
	QCOMPARE(clock.seconds(), quint32(0));

	QSignalSpy spy(&clock, &SystemClock::dateChanged);
	now = makeLocal(2026, 7, 14, 10, 42, 17);
	clock.resync();
	QVERIFY(spy.count() >= 1);
	QCOMPARE(clock.hours(), quint32(10));
	QCOMPARE(clock.minutes(), quint32(42));
	// Minutes precision zeros seconds.
	QCOMPARE(clock.seconds(), quint32(0));
}

void TestSystemClock::forwardJumpConvergesOnTimeout() {
	// Without resync, a forward wall-clock jump is applied on the next timer fire
	// (explicit immediate convergence requires resync; timer path uses >500ms skew).
	auto now = makeLocal(2026, 7, 14, 10, 0, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.resync();
	QCOMPARE(clock.minutes(), quint32(0));

	// Jump two minutes forward before the scheduled minute boundary.
	now = makeLocal(2026, 7, 14, 10, 2, 5);
	clock.testFireTimeout();
	QCOMPARE(clock.hours(), quint32(10));
	QCOMPARE(clock.minutes(), quint32(2));
}

void TestSystemClock::backwardJumpConvergesOnTimeout() {
	auto now = makeLocal(2026, 7, 14, 10, 30, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.resync();
	QCOMPARE(clock.minutes(), quint32(30));

	now = makeLocal(2026, 7, 14, 10, 10, 0);
	clock.testFireTimeout();
	QCOMPARE(clock.minutes(), quint32(10));
}

void TestSystemClock::timezoneOffsetChange() {
	// Simulate an offset/timezone change by moving the provider's offset from UTC.
	// QDateTime local components must follow the provider.
	auto now = QDateTime(QDate(2026, 7, 14), QTime(12, 0, 0), QTimeZone::fromSecondsAheadOfUtc(0));
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.resync();
	QCOMPARE(clock.hours(), quint32(12));

	now = QDateTime(QDate(2026, 7, 14), QTime(12, 0, 0), QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
	// Same wall components in a different offset: resync must pick provider value.
	// Use a shifted local wall time that would differ if offset ignored.
	now = QDateTime(QDate(2026, 7, 14), QTime(20, 15, 0), QTimeZone::fromSecondsAheadOfUtc(8 * 3600));
	clock.resync();
	QCOMPARE(clock.hours(), quint32(20));
	QCOMPARE(clock.minutes(), quint32(15));
}

void TestSystemClock::disableEnable() {
	auto now = makeLocal(2026, 7, 14, 9, 0, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.resync();
	QVERIFY(clock.enabled());

	clock.setEnabled(false);
	QVERIFY(!clock.enabled());
	// While disabled, provider jumps must not auto-schedule; resync still works.
	now = makeLocal(2026, 7, 14, 11, 45, 0);
	clock.resync();
	QCOMPARE(clock.hours(), quint32(11));
	QCOMPARE(clock.minutes(), quint32(45));

	now = makeLocal(2026, 7, 14, 12, 0, 0);
	clock.setEnabled(true);
	// setEnabled(true) calls update() which samples wall clock.
	QCOMPARE(clock.hours(), quint32(12));
	QCOMPARE(clock.minutes(), quint32(0));
}

void TestSystemClock::targetSkewWithin500msUsesTarget() {
	// setTime uses target when |wall - target| < 500ms.
	auto now = makeLocal(2026, 7, 14, 10, 0, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Seconds);
	clock.resync();

	// Predict target one second ahead; wall within 500ms of that target.
	auto target = makeLocal(2026, 7, 14, 10, 0, 1);
	now = makeLocal(2026, 7, 14, 10, 0, 0).addMSecs(900); // 100ms before target
	// Drive setTime via timeout with known target by temporarily using public path:
	// resync samples wall; for in-band target use testFire after schedule.
	// Directly verify via timeout: schedule from 10:00:00 sets target ~10:00:01.
	now = makeLocal(2026, 7, 14, 10, 0, 0);
	clock.resync();
	// Fire with wall 100ms before predicted next second target.
	now = makeLocal(2026, 7, 14, 10, 0, 0).addMSecs(900);
	clock.testFireTimeout();
	// Target path should land on second-precision time near 10:00:01.
	QCOMPARE(clock.hours(), quint32(10));
	QCOMPARE(clock.minutes(), quint32(0));
	QCOMPARE(clock.seconds(), quint32(1));
	Q_UNUSED(target);
}

void TestSystemClock::targetSkewBeyond500msUsesWallClock() {
	auto now = makeLocal(2026, 7, 14, 10, 0, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Seconds);
	clock.resync();

	// Large forward jump: timer target is ~1s ahead of original, wall is minutes ahead.
	now = makeLocal(2026, 7, 14, 10, 5, 30);
	clock.testFireTimeout();
	QCOMPARE(clock.hours(), quint32(10));
	QCOMPARE(clock.minutes(), quint32(5));
	QCOMPARE(clock.seconds(), quint32(30));
}

void TestSystemClock::resyncWhileDisabledStillUpdatesDate() {
	auto now = makeLocal(2026, 7, 14, 8, 0, 0);
	SystemClock clock;
	clock.setNowProvider([&]() { return now; });
	clock.setPrecision(SystemClock::Minutes);
	clock.setEnabled(false);
	now = makeLocal(2026, 7, 14, 8, 33, 0);
	clock.resync();
	QCOMPARE(clock.minutes(), quint32(33));
	QCOMPARE(clock.hours(), quint32(8));
}

void TestSystemClock::noUpdateNowOrRefreshAliases() {
	// Sole authorized entry is resync(); no updateNow()/refresh() QMetaObject methods.
	const auto* meta = &SystemClock::staticMetaObject;
	QVERIFY(meta->indexOfMethod("resync()") >= 0);
	QVERIFY(meta->indexOfMethod("updateNow()") < 0);
	QVERIFY(meta->indexOfMethod("refresh()") < 0);
}

QTEST_MAIN(TestSystemClock);
