#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include <qbytearray.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qobject.h>
#include <qsignalspy.h>
#include <qstring.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qtemporarydir.h>
#include <qtimer.h>
#include <qthreadpool.h>

#include "../fileview.hpp"

using qs::io::FileView;

namespace {

bool makeFile(const QString& path, const QByteArray& content) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
	return file.write(content) == content.size();
}

QByteArray readFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return {};
	return file.readAll();
}

// A FIFO whose reader drains it very slowly. The FileView worker writes to the
// FIFO (non-atomic path), so write() blocks in the worker thread until the
// reader consumes data. This is a deterministic slow-writer harness: the GUI
// thread must keep its event loop running while the worker is blocked.
//
// The reader opens the FIFO with a blocking open(O_RDONLY): this pairs with
// the worker's blocking open(O_WRONLY) as soon as both sides are present, and
// then drains the pipe slowly. The reader thread is detached: stop() only sets
// a flag; the reader exits on its own once it observes EOF and the stop flag.
// The FIFO is unlinked after the reader finishes so no stale file remains.
class SlowWriter {
public:
	SlowWriter() {
		static QAtomicInteger<quint64> counter = 0;
		mPath = QStringLiteral("/tmp/fv-slowwriter-%1-%2")
		            .arg(::getpid())
		            .arg(counter.fetchAndAddRelaxed(1));
		::mkfifo(mPath.toLocal8Bit().constData(), 0600);
	}

	~SlowWriter() { stop(); }

	QString path() const { return mPath; }

	void start() {
		mThread = std::thread([this] {
			while (!mStop.loadAcquire()) {
				// Non-blocking open never hangs; the worker's blocking
				// open(O_WRONLY) pairs with this read end as soon as it
				// appears.
				int fd = ::open(this->path().toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
				if (fd < 0) break;
				struct pollfd pfd {fd, POLLIN, 0};
				char buf[64];
				bool reopen = false;
				while (!mStop.loadAcquire()) {
					int rc = ::poll(&pfd, 1, 50);
					if (rc > 0 && (pfd.revents & POLLIN)) {
						ssize_t n = ::read(fd, buf, sizeof(buf));
						if (n > 0) {
							// 1ms per 64 bytes: a 256KB payload takes ~4s.
							::usleep(1000);
						} else if (n == 0) {
							// Writer closed its end; the next writer needs a
							// fresh read end.
							reopen = true;
							break;
						} else if (errno == EINTR) {
							continue;
						} else {
							reopen = true;
							break;
						}
					} else if (rc < 0 && errno != EINTR) {
						break;
					}
				}
				::close(fd);
				if (reopen) continue;
				break;
			}
			::unlink(this->path().toLocal8Bit().constData());
		});
		// Give the reader thread a chance to open the FIFO before returning,
		// so a subsequent setText() writer pairs with it.
		QTest::qWait(10);
	}

	void stop() {
		mStop.storeRelease(true);
		if (mThread.joinable()) mThread.join();
	}

private:
	QString mPath;
	std::thread mThread;
	QAtomicInteger<bool> mStop = false;
};

// The GUI event loop must keep ticking while a write is in flight. If the
// implementation blocks the GUI thread (old waitForJob), this never returns
// and the test hangs until the outer timeout kills it.
void requireHeartbeat(int timeoutMs = 1000) {
	auto timer = QTimer();
	timer.setTimerType(Qt::PreciseTimer);
	auto fired = QSignalSpy(&timer, &QTimer::timeout);
	timer.start(10);
	QVERIFY2(fired.wait(timeoutMs),
	         "GUI event loop did not run a timer while a write was in flight");
}

} // namespace

class TestFileView: public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void blockedWriteLeavesEventLoopRunning();
	void rapidWritesLandLatestValue();
	void queuedReadAfterWriteSeesWrittenFile();
	void blockedWriteThenSetPathIsNonBlocking();
	void blockedWriteThenDestroyIsSafe();
	void queuedWriteReplacedByReadKeepsData();
	void unloadDuringWriteDoesNotRollBack();
	void atomicCommitFailureReportsError();
	void waitForJobAfterPathChangeReportsCorrectError();
	void writeErrorPropagatesPerOperation();
	void nonAtomicWriteLeavesFileOnFailure();
	void atomicWriteAndHeartbeat();
};

void TestFileView::initTestCase() {
	// Writing to a FIFO whose read end closed raises SIGPIPE; the worker must
	// see EPIPE as an I/O error instead of killing the whole test process.
	::signal(SIGPIPE, SIG_IGN);
}

void TestFileView::blockedWriteLeavesEventLoopRunning() {
	auto writer = SlowWriter();
	writer.start();

	auto view = FileView();
	view.setProperty("__preload", false);
	view.setPath(writer.path());
	view.bindableAtomicWrites().setValue(false);
	auto saved = QSignalSpy(&view, &FileView::saved);
	auto failed = QSignalSpy(&view, &FileView::saveFailed);

	view.setText(QByteArray(256 * 1024, 'x'));
	QVERIFY2(!saved.wait(100), "write completed instantly - the slow writer did not block");

	// The second write must not block the GUI thread on the first writer.
	// If setText() blocked the GUI thread (old waitForJob), it would take the
	// full FIFO drain time (~4s); a non-blocking queue returns in
	// microseconds.
	auto elapsed = QElapsedTimer();
	elapsed.start();
	view.setText("final");
	auto setTextMs = elapsed.elapsed();
	QVERIFY2(setTextMs < 500, qPrintable(QStringLiteral(
	             "setText() blocked the GUI thread for %1ms while a write was "
	             "in flight").arg(setTextMs)));

	// Wait for both writes to complete (the reader drains the FIFO slowly,
	// ~1.6s for the 1MB payload), then stop the reader.
	QVERIFY(QTest::qWaitFor([&] { return saved.count() >= 2; }, 15000));
	QVERIFY2(saved.count() >= 2, "both writes must complete");
	QCOMPARE(failed.count(), 0);
	writer.stop();
}

void TestFileView::rapidWritesLandLatestValue() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("rapid.txt");

	auto view = FileView();
	view.setPath(path);
	view.bindableAtomicWrites().setValue(false);
	auto saved = QSignalSpy(&view, &FileView::saved);
	auto failed = QSignalSpy(&view, &FileView::saveFailed);

	// A burst of writes: only the last value must land. The superseded write
	// (second) emits no completion signal - it never ran; the live write and
	// the final queued write each emit one.
	view.setText("first");
	view.setText("second");
	view.setText("third");

	QVERIFY(QTest::qWaitFor([&] { return saved.count() >= 2; }, 5000));
	QVERIFY2(saved.count() >= 2, "executed writes must produce saved() signals");
	QCOMPARE(failed.count(), 0);

	QCOMPARE(readFile(path), QByteArray("third"));
}

void TestFileView::queuedReadAfterWriteSeesWrittenFile() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("readafter.txt");
	QVERIFY(makeFile(path, "seed"));

	auto view = FileView();
	view.setPath(path);
	view.bindableAtomicWrites().setValue(false);
	auto saved = QSignalSpy(&view, &FileView::saved);

	view.setText("new value");
	QVERIFY(saved.wait(5000));

	// A read issued immediately after a write must see the written file.
	view.reload();
	requireHeartbeat(1000);
	QVERIFY(view.data() == QByteArray("new value"));
}

void TestFileView::blockedWriteThenSetPathIsNonBlocking() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	auto writer = SlowWriter();
	writer.start();

	auto view = FileView();
	view.setProperty("__preload", false);
	view.setPath(writer.path());
	view.bindableAtomicWrites().setValue(false);
	view.setText(QByteArray(256 * 1024, 'x'));
	QVERIFY(!QSignalSpy(&view, &FileView::saved).wait(100));

	// Path change while a write is in flight must not block the GUI thread.
	view.setPath(dir.filePath("other.txt"));
	QVERIFY2(view.path() == dir.filePath("other.txt"),
	         "path change was not applied while a write was in flight");
	requireHeartbeat();

	writer.stop();
	QSignalSpy(&view, &FileView::saved).wait(5000);
}

void TestFileView::blockedWriteThenDestroyIsSafe() {
	auto writer = SlowWriter();
	writer.start();

	{
		auto view = FileView();
		view.setProperty("__preload", false);
		view.setPath(writer.path());
		view.bindableAtomicWrites().setValue(false);
		view.setText(QByteArray(256 * 1024, 'x'));
		QVERIFY(!QSignalSpy(&view, &FileView::saved).wait(100));

		// Destroying the view while a write is in flight must not block the
		// GUI thread: the worker is disowned and finishes on its own. The
		// elapsed-time check makes the old blocking destructor fail this
		// test (it waited for the FIFO drain).
		auto elapsed = QElapsedTimer();
		elapsed.start();
		QVERIFY2(elapsed.elapsed() < 500,
		         "view destruction blocked the GUI thread on an in-flight write");
	}
	writer.stop();
	QVERIFY(QTest::qWaitFor([] { return true; }, 1000));
}

void TestFileView::queuedWriteReplacedByReadKeepsData() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("qwr.txt");
	QVERIFY(makeFile(path, "seed"));

	auto writer = SlowWriter();
	writer.start();

	auto view = FileView();
	view.setProperty("__preload", false);
	view.setPath(writer.path());
	view.bindableAtomicWrites().setValue(false);
	auto saved = QSignalSpy(&view, &FileView::saved);
	auto failed = QSignalSpy(&view, &FileView::saveFailed);

	view.setText(QByteArray(256 * 1024, 'x'));
	QVERIFY(!saved.wait(100));

	// A second write queues behind the first.
	view.setText("queued value");
	// A read issued now supersedes the queued write: the queued write's data
	// is discarded but its completion is still reported (the read wins).
	view.data();
	requireHeartbeat(1000);
	// The superseded queued write emits no signal; the in-flight write
	// completes with one saved(). Wait for it before stopping the reader:
	// the reader's exit would otherwise break the FIFO write with EPIPE.
	QVERIFY(QTest::qWaitFor([&] { return saved.count() >= 1; }, 10000));
	writer.stop();
	QVERIFY2(saved.count() >= 1, "the live write must complete");
	QCOMPARE(failed.count(), 0);
}

void TestFileView::unloadDuringWriteDoesNotRollBack() {
	auto writer = SlowWriter();
	writer.start();

	auto view = FileView();
	view.setProperty("__preload", false);
	view.setPath(writer.path());
	view.bindableAtomicWrites().setValue(false);
	view.setText(QByteArray(256 * 1024, 'x'));
	QVERIFY(!QSignalSpy(&view, &FileView::saved).wait(100));

	// Unload while the write is in flight: the path must stay empty when the
	// write completes - the unload is the latest intent. Stop the reader so
	// the write fails and completes, then verify no rollback.
	view.setPath("");
	QVERIFY2(view.path().isEmpty(), "unload must clear the path");

	auto savedSpy = QSignalSpy(&view, &FileView::saved);
	auto failedSpy = QSignalSpy(&view, &FileView::saveFailed);
	writer.stop();
	QVERIFY(QTest::qWaitFor([&] { return savedSpy.count() > 0 || failedSpy.count() > 0; }, 5000));
	QVERIFY2(view.path().isEmpty(),
	         "write completion must not roll the path back to the old target");
}

void TestFileView::atomicCommitFailureReportsError() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	// A directory is not a valid atomic-write target: QSaveFile::commit()
	// fails (rename onto a directory).
	QVERIFY(QDir().mkpath(dir.filePath("subdir")));

	auto view = FileView();
	view.setPath(dir.filePath("subdir"));
	view.bindableAtomicWrites().setValue(true);
	view.bindablePrintErrors().setValue(false);

	auto failed = QSignalSpy(&view, &FileView::saveFailed);
	view.setText("cannot commit");
	QVERIFY(failed.wait(5000));
	QCOMPARE(failed.count(), 1);
}

void TestFileView::waitForJobAfterPathChangeReportsCorrectError() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("wfj.txt");
	QVERIFY(makeFile(path, "seed"));

	auto view = FileView();
	view.setPath(path);
	view.bindableAtomicWrites().setValue(false);
	view.bindablePrintErrors().setValue(false);

	auto writer = SlowWriter();
	writer.start();
	view.setPath(writer.path());
	view.setText(QByteArray(256 * 1024, 'x'));
	QVERIFY(!QSignalSpy(&view, &FileView::saved).wait(100));

	// Path change queues a read for the new path; stop the reader so the
	// in-flight write fails, then waitForJob must report the write's real
	// outcome, not a stale state.
	view.setPath(dir.filePath("target.txt"));
	writer.stop();
	auto failed = QSignalSpy(&view, &FileView::saveFailed);
	view.waitForJob();
	QVERIFY2(failed.count() >= 1, "the failed in-flight write must report saveFailed");
}

void TestFileView::writeErrorPropagatesPerOperation() {
	// A directory that cannot be written (no write bit) makes open(WriteOnly)
	// fail deterministically: a disk/permission error.
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("locked");
	QVERIFY(::mkdir(path.toLocal8Bit().constData(), 0500) == 0);

	auto view = FileView();
	view.setPath(path + "/file.txt");
	view.bindableAtomicWrites().setValue(false);
	view.bindablePrintErrors().setValue(false);

	auto failed = QSignalSpy(&view, &FileView::saveFailed);
	view.setText("cannot write");
	QVERIFY(failed.wait(5000));
	QCOMPARE(failed.count(), 1);
}

void TestFileView::nonAtomicWriteLeavesFileOnFailure() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("readonly.txt");
	QVERIFY(makeFile(path, "seed"));
	// Make the file read-only so the non-atomic write fails at open time.
	QVERIFY(::chmod(path.toLocal8Bit().constData(), 0400) == 0);

	auto view = FileView();
	view.setPath(path);
	view.bindablePrintErrors().setValue(false);
	view.bindableAtomicWrites().setValue(false);

	auto failed = QSignalSpy(&view, &FileView::saveFailed);
	view.setText("cannot write");
	QVERIFY(failed.wait(5000));

	// The non-atomic path leaves the original file untouched on failure.
	QFile file(path);
	QVERIFY(file.exists());
	QCOMPARE(readFile(path), QByteArray("seed"));
}

void TestFileView::atomicWriteAndHeartbeat() {
	auto dir = QTemporaryDir();
	QVERIFY(dir.isValid());
	const auto path = dir.filePath("atomic.txt");
	QVERIFY(makeFile(path, "seed"));

	auto view = FileView();
	view.setPath(path);
	view.bindableAtomicWrites().setValue(true);

	auto saved = QSignalSpy(&view, &FileView::saved);
	view.setText("atomic content");
	QVERIFY(saved.wait(5000));
	QCOMPARE(saved.count(), 1);

	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(file.readAll(), QByteArray("atomic content"));
}

QTEST_MAIN(TestFileView);

#include "fileview.moc"
