#include "desktopentrymonitor.hpp"

#include <qdeadlinetimer.h>
#include <qdir.h>
#include <qfile.h>
#include <qsignalspy.h>
#include <qtemporarydir.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qtextstream.h>

#include "../desktopentrymonitor.hpp"

namespace {

bool writeDesktop(
    const QString& path,
    const QString& name,
    const QString& exec = QStringLiteral("true")
) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
	QTextStream out(&file);
	out << "[Desktop Entry]\n";
	out << "Type=Application\n";
	out << "Name=" << name << "\n";
	out << "Exec=" << exec << "\n";
	out << "Icon=app\n";
	file.close();
	return true;
}

bool waitForSignal(QSignalSpy& spy, int minCount, int timeoutMs = 2000) {
	const auto deadline = QDeadlineTimer(timeoutMs);
	while (spy.count() < minCount) {
		if (deadline.hasExpired()) return false;
		if (!spy.wait(qMin(200, int(deadline.remainingTime())))) {
			// wait returns false on timeout for that slice; keep looping until deadline
			if (deadline.hasExpired()) return false;
		}
	}
	return spy.count() >= minCount;
}

} // namespace

void TestDesktopEntryMonitor::createDesktopFileEmitsChange() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));

	DesktopEntryMonitor monitor(QStringList {root});
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	const auto path = root + QStringLiteral("/hello.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("Hello")));
	QVERIFY2(waitForSignal(spy, 1), "creating a desktop file must emit after debounce");
}

void TestDesktopEntryMonitor::inPlaceContentModifyEmitsChange() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));
	const auto path = root + QStringLiteral("/edit.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("Before")));

	DesktopEntryMonitor monitor(QStringList {root});
	// Allow initial watch registration to settle; no signal expected yet.
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	// In-place overwrite of an already-watched file (the historical gap).
	QVERIFY(writeDesktop(path, QStringLiteral("After"), QStringLiteral("false")));
	QVERIFY2(
	    waitForSignal(spy, 1),
	    "in-place Name/Exec modification of an existing desktop file must emit"
	);
}

void TestDesktopEntryMonitor::atomicRenameOverwriteEmitsChange() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));
	const auto path = root + QStringLiteral("/atomic.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("Old")));

	DesktopEntryMonitor monitor(QStringList {root});
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	const auto tmpPath = root + QStringLiteral("/.atomic.desktop.tmp");
	QVERIFY(writeDesktop(tmpPath, QStringLiteral("New"), QStringLiteral("echo")));
	// Atomic replace: rename over the watched file.
	QVERIFY(QFile::remove(path) || !QFile::exists(path));
	QVERIFY(QFile::rename(tmpPath, path));

	QVERIFY2(waitForSignal(spy, 1), "atomic rename overwrite must emit a change");
}

void TestDesktopEntryMonitor::deleteDesktopFileEmitsChange() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));
	const auto path = root + QStringLiteral("/gone.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("Gone")));

	DesktopEntryMonitor monitor(QStringList {root});
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	QVERIFY(QFile::remove(path));
	QVERIFY2(waitForSignal(spy, 1), "deleting a desktop file must emit a change");
}

void TestDesktopEntryMonitor::newSubdirThenFileEmitsChange() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));

	DesktopEntryMonitor monitor(QStringList {root});
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	const auto sub = root + QStringLiteral("/nested");
	QVERIFY(QDir().mkpath(sub));
	// Directory creation should schedule a rebuild; wait for at least that signal
	// so the new subdir is watched before we add a file.
	QVERIFY2(waitForSignal(spy, 1), "new subdirectory must emit so watches are rebuilt");
	const auto countAfterSubdir = spy.count();

	const auto path = sub + QStringLiteral("/nested-app.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("Nested")));
	QVERIFY2(
	    waitForSignal(spy, countAfterSubdir + 1),
	    "desktop file in a newly created subdirectory must emit"
	);
}

void TestDesktopEntryMonitor::rapidEventsDebounceToOneSignal() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));

	DesktopEntryMonitor monitor(QStringList {root});
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	// Burst of creates within one debounce window (100ms).
	for (int i = 0; i < 5; ++i) {
		const auto path = root + QStringLiteral("/burst%1.desktop").arg(i);
		QVERIFY(writeDesktop(path, QStringLiteral("Burst%1").arg(i)));
	}

	QVERIFY(waitForSignal(spy, 1));
	// Give extra time; still only one coalesced emission for the burst.
	QTest::qWait(250);
	QCOMPARE(spy.count(), 1);
}

void TestDesktopEntryMonitor::watcherStillWorksAfterRescan() {
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const auto root = tmp.path() + QStringLiteral("/applications");
	QVERIFY(QDir().mkpath(root));
	const auto path = root + QStringLiteral("/again.desktop");
	QVERIFY(writeDesktop(path, QStringLiteral("One")));

	DesktopEntryMonitor monitor(QStringList {root});
	QTest::qWait(150);
	QSignalSpy spy(&monitor, &DesktopEntryMonitor::desktopEntriesChanged);

	// First in-place edit triggers processChanges + rebuildWatches.
	QVERIFY(writeDesktop(path, QStringLiteral("Two")));
	QVERIFY(waitForSignal(spy, 1));
	const auto afterFirst = spy.count();

	// Second edit after rebuild must still be observed (watcher re-armed).
	QVERIFY(writeDesktop(path, QStringLiteral("Three")));
	QVERIFY2(
	    waitForSignal(spy, afterFirst + 1),
	    "watcher must remain effective after processChanges rebuild"
	);
}

QTEST_MAIN(TestDesktopEntryMonitor);
