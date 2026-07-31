#include "colorquantizer.hpp"

#include <qatomic.h>
#include <qcolor.h>
#include <qelapsedtimer.h>
#include <qimage.h>
#include <qlist.h>
#include <qrunnable.h>
#include <qsemaphore.h>
#include <qsignalspy.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qtemporaryfile.h>
#include <qthread.h>
#include <qthreadpool.h>
#include <qtmetamacros.h>
#include <qurl.h>

#include <memory>

#include "../colorquantizer.hpp"

namespace {

// A pool job that sleeps for a fixed duration. Used to keep the global thread
// pool non-idle so that the old cancelAsync()'s waitForDone() would block the
// GUI thread for the full sleep window. It self-deletes (autoDelete) so the
// test does not manage its lifetime.
class SleeperRunnable: public QRunnable {
public:
	explicit SleeperRunnable(int ms, QAtomicInt* startedFlag): ms(ms), startedFlag(startedFlag) {
		this->setAutoDelete(true);
	}

	void run() override {
		this->startedFlag->storeRelease(1);
		QThread::msleep(this->ms);
	}

private:
	int ms;
	QAtomicInt* startedFlag;
};

// A pool job that blocks on a semaphore until released. Used to queue a
// quantization operation behind a held pool thread, then release it so the
// worker reads its source immediately after the ColorQuantizer is destroyed
// (before ASan's quarantine recycles the freed block).
class GateRunnable: public QRunnable {
public:
	explicit GateRunnable(QSemaphore* gate, QAtomicInt* startedFlag): gate(gate), startedFlag(startedFlag) {
		this->setAutoDelete(true);
	}

	void run() override {
		this->startedFlag->storeRelease(1);
		this->gate->acquire();
	}

private:
	QSemaphore* gate;
	QAtomicInt* startedFlag;
};

// RAII guard that saves the global pool's maxThreadCount on construction and
// restores it (after draining the pool) on destruction. Destruction runs
// during stack unwinding, so this restores even when a QVERIFY/QCOMPARE
// returns from the test early on failure -- the plain setMaxThreadCount() at
// the end of a slot would be skipped in that case.
class PoolThreadGuard {
public:
	PoolThreadGuard(): pool(QThreadPool::globalInstance()), saved(this->pool->maxThreadCount()) {}

	~PoolThreadGuard() {
		this->pool->waitForDone(5000);
		this->pool->setMaxThreadCount(this->saved);
	}

private:
	QThreadPool* pool;
	int saved;
};

QTemporaryFile* makeSolidImage(const QColor& color) {
	auto* file = new QTemporaryFile();
	file->setAutoRemove(true);
	if (!file->open()) {
		delete file;
		return nullptr;
	}
	auto image = QImage(100, 100, QImage::Format_RGB32);
	image.fill(color.rgb());
	image.save(file, "PNG");
	file->close();
	return file;
}

} // namespace

void TestColorQuantizer::initTestCase() {
	this->redImage = makeSolidImage(QColor(255, 0, 0));
	this->blueImage = makeSolidImage(QColor(0, 0, 255));
	QVERIFY2(this->redImage != nullptr, "failed to create red test image");
	QVERIFY2(this->blueImage != nullptr, "failed to create blue test image");
}

void TestColorQuantizer::cleanupTestCase() {
	delete this->redImage;
	delete this->blueImage;
}

void TestColorQuantizer::cancelAsyncDoesNotBlockGuiThread() {
	// Regression test for S-L2: ColorQuantizer::cancelAsync() used to call
	// QThreadPool::globalInstance()->waitForDone(), blocking the GUI thread on
	// the *global* pool and dropping frames on every wallpaper change.
	//
	// Pin the pool to a single worker and park it with a 300ms sleeper, then
	// supersede a queued quantization with a new source. Between componentComplete()
	// and the superseding setSource() the event loop is never spun, so the first
	// operation's finished() (queued back to this thread) has not run yet and
	// liveOperation is still set -- i.e. cancelAsync() is on the hot path.
	//
	// Old code: cancelAsync -> waitForDone -> blocks until the sleeper exits
	//           (~300ms) -> this assertion fails.
	// New code: cancelAsync sets the cancel flag, disconnects done (discard) and
	//           returns; the operation self-deletes via its deferred finished()
	//           slot -> assertion passes.

	auto* pool = QThreadPool::globalInstance();
	PoolThreadGuard guard; // restores maxThreadCount on scope exit, incl. on QVERIFY failure
	pool->setMaxThreadCount(1);

	QAtomicInt sleeperStarted(0);
	pool->start(new SleeperRunnable(300, &sleeperStarted));
	while (sleeperStarted.loadAcquire() == 0) {
		QTest::qWait(1);
	}

	auto quantizer = ColorQuantizer();
	QSignalSpy colorsSpy(&quantizer, &ColorQuantizer::colorsChanged);

	quantizer.setSource(QUrl::fromLocalFile(this->redImage->fileName()));
	quantizer.classBegin();
	quantizer.componentComplete(); // starts operation 1 on the pool

	// Supersede operation 1. Must return without waiting on the global pool.
	auto timer = QElapsedTimer();
	timer.start();
	quantizer.setSource(QUrl::fromLocalFile(this->blueImage->fileName()));
	auto elapsed = timer.elapsed();

	QVERIFY2(
	    elapsed < 100,
	    qPrintable(QStringLiteral("cancelAsync blocked the GUI thread for %1 ms "
	                              "(expected < 100 without waitForDone)")
	                 .arg(elapsed))
	);

	// The sleeper exits at ~300ms, then operation 1 bails (cancelled) and
	// self-deletes and operation 2 runs and reports the blue result. The
	// colorsSpy.wait() spin also drains the operations' deferred finished()
	// self-deletes; the waitForDone() below is only a safety net to confirm no
	// pool worker is still running before teardown.
	QVERIFY(colorsSpy.wait(5000));
	auto colors = quantizer.bindableColors().value();
	QCOMPARE(colors.size(), 1);
	QCOMPARE(colors.first(), QColor(0, 0, 255));

	// guard drains the pool and restores maxThreadCount on scope exit.
}

void TestColorQuantizer::destroyWhileOperationQueued() {
	// Teardown-safety test for the non-blocking cancelAsync(): once cancelAsync
	// no longer drains the worker, a queued operation can outlive its
	// ColorQuantizer. The operation owns a *copy* of the QUrl, is unparented
	// (so it survives the quantizer), its done() signal auto-disconnects on the
	// receiver side, and it self-deletes via its deferred finished() slot.
	//
	// This verifies that path does not crash or leak when the quantizer is
	// destroyed while an operation is queued. It is NOT a UAF regression catcher
	// for the old QUrl* source = &mSource design: the worker's freed-memory read
	// happens through Qt system-library calls (QUrl/QImage) that are not built
	// with -fsanitize=address, so ASan cannot observe those accesses. The copy
	// fix is verified by inspection and by ASan-clean teardown here.

	auto* pool = QThreadPool::globalInstance();
	PoolThreadGuard guard; // restores maxThreadCount on scope exit, incl. on QVERIFY failure
	pool->setMaxThreadCount(1);

	// Hold the single pool thread at the gate so the operation cannot start.
	QSemaphore gate;
	QAtomicInt gateStarted(0);
	pool->start(new GateRunnable(&gate, &gateStarted));
	while (gateStarted.loadAcquire() == 0) {
		QTest::qWait(1);
	}

	{
		auto quantizer = std::make_unique<ColorQuantizer>();
		quantizer->setSource(QUrl::fromLocalFile(this->blueImage->fileName()));
		quantizer->classBegin();
		quantizer->componentComplete(); // operation enqueued, not yet running
		// Destroy the ColorQuantizer while the operation is still queued.
	}

	// Release the gate: the worker now runs, reads its own QUrl snapshot, and
	// self-deletes. ASan (leak detector) confirms no leak; no crash occurs.
	gate.release(1);
	QTest::qWait(500);
	// guard drains the pool and restores maxThreadCount on scope exit.
}

QTEST_MAIN(TestColorQuantizer);
