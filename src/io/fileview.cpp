#include "fileview.hpp"
#include <array>
#include <utility>

#include <qatomic.h>
#include <qdir.h>
#include <qfiledevice.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmutex.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qqmlinfo.h>
#include <qsavefile.h>
#include <qscopedpointer.h>
#include <qthreadpool.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/logcat.hpp"
#include "../core/util.hpp"

namespace qs::io {

namespace {
QS_LOGGING_CATEGORY(logFileView, "quickshell.io.fileview", QtWarningMsg);
}

QString FileViewError::toString(FileViewError::Enum value) {
	switch (value) {
	case Success: return "Success";
	case Unknown: return "An unknown error has occurred";
	case FileNotFound: return "The specified file does not exist";
	case PermissionDenied: return "Permission denied";
	case NotAFile: return "The specified path was not a file";
	default: return "Invalid error";
	}
}

bool FileViewData::operator==(const FileViewData& other) const {
	if (this->data == other.data && !this->data.isEmpty()) return true;
	if (this->text == other.text && !this->text.isEmpty()) return true;
	return this->operator const QByteArray&() == other.operator const QByteArray&();
}

bool FileViewData::isEmpty() const { return this->data.isEmpty() && this->text.isEmpty(); }

FileViewData::operator const QString&() const {
	if (this->text.isEmpty() && !this->data.isEmpty()) {
		this->text = QString::fromUtf8(this->data);
	}

	return this->text;
}

FileViewData::operator const QByteArray&() const {
	if (this->data.isEmpty() && !this->text.isEmpty()) {
		this->data = this->text.toUtf8();
	}

	return this->data;
}

FileViewOperation::FileViewOperation(FileView* owner): owner(owner) {
	this->setAutoDelete(false);
	this->blockMutex.lock();
}

void FileViewOperation::block() {
	// block until a lock can be acauired, then immediately drop it
	auto unused = QMutexLocker(&this->blockMutex);
}

void FileViewOperation::tryCancel() { this->shouldCancel.storeRelease(true); }

void FileViewOperation::disposePending() {
	// The operation never ran, so it is still holding its block mutex and
	// finishRun() will never be called. Release the mutex before deleting.
	this->blockMutex.unlock();
	delete this;
}

void FileViewOperation::finishRun() {
	this->blockMutex.unlock();

	// The owning FileView may have been destroyed while this worker ran
	// (destructor disowns the live operation). In that case the queued
	// completion would rely on an event loop that may never run again (e.g.
	// process shutdown), leaking this object. Self-delete from the worker
	// thread instead: no QML/Qt logging happens with a null owner, so
	// deleting on the worker thread is safe.
	if (!this->owner) {
		delete this;
		return;
	}

	QMetaObject::invokeMethod(this, &FileViewOperation::finished, Qt::QueuedConnection);
}

void FileViewOperation::finished() {
	emit this->done();
	// Delete happens on the main thread, after done(), meaning no operation accesses
	// will be a UAF.
	delete this;
}

void FileViewReader::run() {
	if (!this->shouldCancel && this->owner) {
		FileViewReader::read(
		    this->owner,
		    this->state,
		    this->doStringConversion,
		    this->shouldCancel
		);

		if (this->shouldCancel.loadAcquire()) {
			qCDebug(logFileView) << "Read" << this << "of" << this->state.path << "canceled for"
			                     << this->owner;
		}
	}

	this->finishRun();
}

void FileViewReader::read(
    const QPointer<FileView>& view,
    FileViewState& state,
    bool doStringConversion,
    const QAtomicInteger<bool>& shouldCancel
) {
	qCDebug(logFileView) << "Reader started for" << state.path;

	// The owning FileView may have been destroyed while this worker ran. The
	// QPointer is cleared atomically by ~QObject, so a null check here (and
	// before every qmlWarning below) keeps QML object access off freed
	// memory. File I/O and state updates remain valid in that case.
	if (!view) {
		state.error = FileViewError::Unknown;
		return;
	}

	auto info = QFileInfo(state.path);
	state.exists = info.exists();

	if (!state.exists) {
		if (state.printErrors) {
			if (view) qmlWarning(view) << "Read of " << state.path << " failed: File does not exist.";
		}

		state.error = FileViewError::FileNotFound;
		return;
	}

	if (!info.isFile()) {
		if (state.printErrors) {
			if (view) qmlWarning(view) << "Read of " << state.path << " failed: Not a file.";
		}

		state.error = FileViewError::NotAFile;
		return;
	} else if (!info.isReadable()) {
		if (state.printErrors) {
			if (view) qmlWarning(view) << "Read of " << state.path << " failed: Permission denied.";
		}

		state.error = FileViewError::PermissionDenied;
		return;
	}

	if (shouldCancel.loadAcquire()) return;

	auto file = QFile(state.path);

	if (!file.open(QFile::ReadOnly)) {
		if (view) qmlWarning(view) << "Read of " << state.path
		                           << " failed: Unknown failure when opening file.";
		state.error = FileViewError::Unknown;
		return;
	}

	if (shouldCancel.loadAcquire()) return;

	if (file.size() != 0) {
		auto data = QByteArray(file.size(), Qt::Uninitialized);
		qint64 i = 0;

		while (true) {
			if (shouldCancel.loadAcquire()) return;

			auto r = file.read(data.data() + i, data.length() - i); // NOLINT

			if (r == -1) {
				if (view) qmlWarning(view) << "Read of " << state.path << " failed: read() failed.";

				state.error = FileViewError::Unknown;
				return;
			} else if (r == 0) {
				data.resize(i);
				break;
			}

			i += r;
		}

		state.data = data;
	} else { // Mostly happens in /proc and friends, which have zero sized files with content.
		QByteArray data;
		auto buf = std::array<char, 4096>();

		while (true) {
			if (shouldCancel.loadAcquire()) return;

			auto r = file.read(buf.data(), buf.size()); // NOLINT

			if (r == -1) {
				if (view) qmlWarning(view) << "Read of " << state.path << " failed: read() failed.";

				state.error = FileViewError::Unknown;
				return;
			} else {
				data.append(buf.data(), r);
				if (r == 0) break;
			}
		}

		state.data = data;
	}

	if (shouldCancel.loadAcquire()) return;

	if (doStringConversion) {
		state.data.operator const QString&();
	}
}

void FileViewWriter::run() {
	if (!this->shouldCancel.loadAcquire() && this->owner) {
		FileViewWriter::write(this->owner, this->state, this->doAtomicWrite, this->shouldCancel);

		if (this->shouldCancel.loadAcquire()) {
			qCDebug(logFileView) << "Write" << this << "of" << this->state.path << "canceled for"
			                     << this->owner;
		}
	}

	this->finishRun();
}

void FileViewWriter::write(
    const QPointer<FileView>& view,
    FileViewState& state,
    bool doAtomicWrite,
    const QAtomicInteger<bool>& shouldCancel
) {
	qCDebug(logFileView) << "Writer started for" << state.path;

	// The owning FileView may have been destroyed while this worker ran. The
	// QPointer is cleared atomically by ~QObject, so a null check here (and
	// before every qmlWarning below) keeps QML object access off freed
	// memory. File I/O and state updates remain valid in that case.
	if (!view) {
		state.error = FileViewError::Unknown;
		return;
	}

	auto info = QFileInfo(state.path);
	state.exists = info.exists();

	if (!state.exists) {
		auto dir = info.dir();
		if (!dir.mkpath(".")) {
			if (state.printErrors) {
				if (view) qmlWarning(view) << "Write of " << state.path
				                 << " failed: Could not create parent directories of file.";
			}

			state.error = FileViewError::PermissionDenied;
			return;
		}
	} else if (!info.isWritable()) {
		if (state.printErrors) {
			if (view) qmlWarning(view) << "Write of " << state.path << " failed: Permission denied.";
		}

		state.error = FileViewError::PermissionDenied;
		return;
	}

	if (shouldCancel.loadAcquire()) return;

	QScopedPointer<QFileDevice> file;
	if (doAtomicWrite) {
		file.reset(new QSaveFile(state.path));
	} else {
		file.reset(new QFile(state.path));
	}

	if (!file->open(QFile::WriteOnly)) {
		if (view) qmlWarning(view) << "Write of " << state.path
		                           << " failed: Unknown error when opening file.";
		state.error = FileViewError::Unknown;
		return;
	}

	if (shouldCancel.loadAcquire()) return;

	const QByteArray& data = state.data;
	qint64 i = 0;

	while (true) {
		if (shouldCancel.loadAcquire()) return;

		auto r = file->write(data.data() + i, data.length() - i); // NOLINT

		if (r == -1) {
			if (view) qmlWarning(view) << "Write of " << state.path << " failed: write() failed.";

			state.error = FileViewError::Unknown;
			return;
		} else {
			i += r;
			if (i == data.length()) break;
		}
	}

	if (shouldCancel.loadAcquire()) return;

	if (doAtomicWrite) {
		if (!reinterpret_cast<QSaveFile*>(file.get())->commit()) {
			if (view) qmlWarning(view) << "Write of " << state.path
			                           << " failed: Atomic commit failed.";
			state.error = FileViewError::Unknown;
		}
	}
}

FileView::~FileView() {
	if (this->mAdapter) {
		this->mAdapter->setFileView(nullptr);
	}

	// A queued operation has never started: dispose it here.
	if (this->pendingOperation) {
		this->pendingOperation->disposePending();
		this->pendingOperation = nullptr;
	}

	// A live operation keeps running on a worker thread. Disown it the same
	// way a cancelled read is disowned: try to cancel, drop the completion
	// connection, and let the worker finish on its own. The worker never
	// touches this object again - read()/write() receive a null view (the
	// QPointer owner is cleared by ~QObject) and return immediately, and no
	// QML/Qt logging runs in that case - so waiting here would only block
	// the GUI thread on a slow or blocked write for no benefit.
	if (this->liveOperation) {
		this->liveOperation->tryCancel();
		QObject::disconnect(this->liveOperation, nullptr, this, nullptr);
		this->liveOperation = nullptr;
	}
}

void FileView::loadAsync(bool doStringConversion) {
	// Writes update via operationFinished, making a read both invalid and outdated.
	// If a write is in flight (or queued), a read must wait for it: queue the
	// read instead of cancelling the write, which would block the GUI thread.
	if (this->liveWriter() || this->pendingWriter()) {
		if (auto* reader = this->pendingReader()) {
			// Latest read wins: a queued read is replaced with the new path.
			reader->state.path = this->targetPath;
			reader->doStringConversion = doStringConversion;
			reader->state.printErrors = this->bPrintErrors;
			this->pathInFlight = this->targetPath;
			return;
		}

		// A queued write is being replaced by a read. Its value is discarded
		// (the target file is being read instead of written); no completion
		// signal is emitted for a write that never runs - the read that
		// supersedes it carries the outcome. writeData is cleared so a later
		// identical write is not deduplicated against the discarded value.
		if (auto* writer = this->pendingWriter()) {
			writer->disposePending();
			this->pendingOperation = nullptr;
			this->writeData = FileViewData();
		}

		// A completion signal handler may have queued a new operation (not
		// possible here - no emit above - but kept for symmetry with
		// saveAsync); never overwrite it without disposing it first.
		if (this->pendingOperation) {
			this->pendingOperation->disposePending();
			this->pendingOperation = nullptr;
		}

		auto* queuedReader = new FileViewReader(this, doStringConversion);
		queuedReader->state.path = this->targetPath;
		queuedReader->state.printErrors = this->bPrintErrors;
		QObject::connect(
		    queuedReader,
		    &FileViewOperation::done,
		    this,
		    &FileView::operationFinished
		);
		this->pendingOperation = queuedReader;
		this->pathInFlight = this->targetPath;
		return;
	}

	if (!this->liveOperation || this->pathInFlight != this->targetPath) {
		// A queued operation that is not for the current target was made
		// stale by a reentrant path change; drop it before starting a fresh
		// live read (a reentrant completion handler may have queued it).
		if (this->pendingOperation
		    && this->pendingOperation->state.path != this->targetPath) {
			this->pendingOperation->disposePending();
			this->pendingOperation = nullptr;
		}

		this->cancelAsync();
		this->pathInFlight = this->targetPath;

		if (this->targetPath.isEmpty()) {
			auto state = FileViewState();
			this->updateState(state);
		} else {
			qCDebug(logFileView) << "Starting async load for" << this << "of" << this->targetPath;
			auto* reader = new FileViewReader(this, doStringConversion);
			reader->state.path = this->targetPath;
			reader->state.printErrors = this->bPrintErrors;
			QObject::connect(reader, &FileViewOperation::done, this, &FileView::operationFinished);
			QThreadPool::globalInstance()->start(reader); // takes ownership
			this->liveOperation = reader;
		}
	}
}

void FileView::saveAsync() {
	if (this->targetPath.isEmpty()) {
		qmlWarning(this) << "Cannot write file, as no path has been specified.";
		this->writeData = FileViewData();
	} else {
		// writeData stays populated until the operation completes so that
		// writeCmpData() keeps deduplicating identical writes (A07.2).
		auto data = this->writeData;

		// A write is already in flight: queue this write behind it. The
		// queued write replaces a previously queued one (latest-wins), so a
		// burst of writes lands only the last value.
		if (this->liveWriter() || this->pendingWriter()) {
			if (auto* writer = this->pendingWriter()) {
				// The queued write is superseded by a newer one: its value is
				// discarded (the new write carries the latest data). No
				// completion signal is emitted for a write that never runs;
				// the superseding write's completion carries the outcome.
				writer->disposePending();
				this->pendingOperation = nullptr;
			} else if (auto* reader = this->pendingReader()) {
				// A queued read is superseded by a new write: the read's data
				// would be outdated once the write lands, so it is dropped.
				reader->disposePending();
				this->pendingOperation = nullptr;
			}

			// A completion signal handler may have queued a new operation
			// during a dispose above (none emits here, but keep the guard);
			// never overwrite it without disposing it first.
			if (this->pendingOperation) {
				this->pendingOperation->disposePending();
				this->pendingOperation = nullptr;
			}

			auto* writer = new FileViewWriter(this, this->bAtomicWrites);
			writer->state.path = this->targetPath;
			writer->state.data = std::move(data);
			writer->state.printErrors = this->bPrintErrors;
			QObject::connect(writer, &FileViewOperation::done, this, &FileView::operationFinished);
			this->pendingOperation = writer;
			return;
		}

		// A queued read has not started yet: a new write supersedes it.
		if (auto* reader = this->pendingReader()) {
			reader->disposePending();
			this->pendingOperation = nullptr;
		}

		this->cancelAsync();

		qCDebug(logFileView) << "Starting async save for" << this << "of" << this->targetPath;
		auto* writer = new FileViewWriter(this, this->bAtomicWrites);
		writer->state.path = this->targetPath;
		writer->state.data = std::move(data);
		writer->state.printErrors = this->bPrintErrors;
		QObject::connect(writer, &FileViewOperation::done, this, &FileView::operationFinished);
		QThreadPool::globalInstance()->start(writer); // takes ownership
		this->liveOperation = writer;
	}
}

void FileView::cancelAsync() {
	// Cancel only a live read: reads are disowned and their completion is
	// ignored. A live write is never cancelled synchronously - that would
	// block the GUI thread until the write finishes. New operations queue
	// behind it instead.
	if (auto* reader = this->liveReader()) {
		qCDebug(logFileView) << "Disowning async read for" << this;
		reader->tryCancel();
		QObject::disconnect(reader, nullptr, this, nullptr);
		this->liveOperation = nullptr;
	}
}

void FileView::startPendingOperation() {
	if (!this->pendingOperation) return;

	auto* next = this->pendingOperation;
	this->pendingOperation = nullptr;

	// A queued operation whose path no longer matches the target was made
	// stale by a reentrant path change; running it would roll the visible
	// state back to an old path. Drop it instead.
	if (next->state.path != this->targetPath) {
		next->disposePending();
		return;
	}

	qCDebug(logFileView) << "Starting queued operation for" << this << "of" << next->state.path;
	QThreadPool::globalInstance()->start(next); // takes ownership
	this->liveOperation = next;

	if (auto* reader = this->liveReader()) {
		this->pathInFlight = reader->state.path;
	}
}

void FileView::operationFinished() {
	auto* finished = this->liveOperation;
	if (this->sender() != finished) {
		qCWarning(logFileView) << "got operation finished from dropped operation" << this->sender();
		return;
	}

	this->liveOperation = nullptr;

	qCDebug(logFileView) << "Async operation finished for" << this;
	this->writeData = FileViewData();

	// A finished write's state carries the path it was started for. If the
	// target path changed while the write was in flight (setPath queued a
	// read for the new path, or set an empty path to unload), applying the
	// writer's old-path state would roll the visible path/data back to the
	// stale location - the unload is the latest intent. The error outcome is
	// still preserved in both cases: the completion signal must reflect the
	// actual result.
	// An empty target is an unload - the newest intent - so a finished
	// write's old-path state must never be applied. Only a matching non-empty
	// target applies the write's state.
	if (finished->state.path == this->targetPath && !this->targetPath.isEmpty()) {
		this->updateState(finished->state);
	} else if (finished->state.error != FileViewError::Success) {
		this->state.error = finished->state.error;
	}

	if (dynamic_cast<FileViewReader*>(finished)) {
		if (finished->state.error) emit this->loadFailed(finished->state.error);
		else emit this->loaded();
	} else {
		if (finished->state.error) emit this->saveFailed(finished->state.error);
		else emit this->saved();
	}

	// A completion-signal handler may have started a new operation
	// (reentrancy); do not let a queued operation clobber it.
	if (this->liveOperation == nullptr) {
		this->startPendingOperation();
	}
}

void FileView::reload() { this->updatePath(); }

bool FileView::waitForJob() {
	// Wait for any live operation to finish. This is the documented blocking
	// API (used by boot-time QML): it blocks the calling thread until the
	// current operation completes, then applies its result.
	if (this->liveOperation != nullptr) {
		QObject::disconnect(this->liveOperation, nullptr, this, nullptr);
		auto* op = this->liveOperation;
		this->liveOperation = nullptr;

		op->block();
		this->writeData = FileViewData();

		// Same stale-path guard as operationFinished: if the target path
		// changed while the operation ran (or was unloaded), do not roll the
		// visible state back to the old path; the queued read brings the new
		// path's data. The error outcome is preserved in both cases.
		if (op->state.path == this->targetPath && !this->targetPath.isEmpty()) {
			this->updateState(op->state);
		} else if (op->state.error != FileViewError::Success) {
			this->state.error = op->state.error;
		}

		if (dynamic_cast<FileViewReader*>(op)) {
			if (op->state.error) emit this->loadFailed(op->state.error);
			else emit this->loaded();
		} else {
			if (op->state.error) emit this->saveFailed(op->state.error);
			else emit this->saved();
		}

		// The completed operation is deleted by its own finished() slot; the
		// disconnect above prevents it from calling operationFinished().
		if (this->liveOperation == nullptr) {
			this->startPendingOperation();
		}
		return true;
	} else return false;
}

void FileView::loadSync() {
	if (this->targetPath.isEmpty()) {
		auto state = FileViewState();
		this->updateState(state);
	} else if (!this->waitForJob()) {
		auto state = FileViewState(this->targetPath);
		state.printErrors = this->bPrintErrors;
		FileViewReader::read(QPointer<FileView>(this), state, false);
		this->updateState(state);

		if (this->state.error) emit this->loadFailed(this->state.error);
		else emit this->loaded();
	}
}

void FileView::saveSync() {
	if (this->targetPath.isEmpty()) {
		qmlWarning(this) << "Cannot write file, as no path has been specified.";
		this->writeData = FileViewData();
	} else {
		// Both reads and writes will be outdated. Capture the data before
		// waiting: waitForJob() clears writeData when a live operation
		// completes.
		auto data = this->writeData;

		// A queued operation must not start concurrently with the synchronous
		// write below: dispose it (its data is superseded by this write).
		if (this->pendingOperation) {
			this->pendingOperation->disposePending();
			this->pendingOperation = nullptr;
		}

		if (this->liveOperation) this->waitForJob();

		auto state = FileViewState(this->targetPath);
		state.data = data;
		state.printErrors = this->bPrintErrors;
		FileViewWriter::write(QPointer<FileView>(this), state, this->bAtomicWrites);
		this->writeData = FileViewData();
		this->updateState(state);

		if (this->state.error) emit this->saveFailed(this->state.error);
		else emit this->saved();
	}
}

void FileView::updateState(FileViewState& newState) {
	DEFINE_DROP_EMIT_IF(newState.path != this->state.path, this, pathChanged);
	// assume if the path was changed the data also changed
	auto dataChanged = pathChanged || newState.data != this->state.data;
	// DEFINE_DROP_EMIT_IF(newState.exists != this->state.exists, this, existsChanged);

	this->mPrepared = true;
	auto loadedChanged = this->setLoadedOrAsync(!newState.path.isEmpty() && newState.exists);

	this->state.path = std::move(newState.path);

	if (dataChanged) {
		this->state.data = newState.data;
	}

	this->state.exists = newState.exists;
	this->state.error = newState.error;

	DropEmitter::call(
	    pathChanged,
	    // existsChanged,
	    loadedChanged
	);

	if (dataChanged) this->emitDataChanged();
}

QString FileView::path() const { return this->state.path; }

void FileView::setPath(const QString& path) {
	auto p = path.startsWith("file://") ? path.sliced(7) : path;
	if (p == this->targetPath) return;

	if (this->liveWriter()) {
		// A write is in flight: do not block the GUI thread. The pending
		// queue holds the new path; the queued read will run after the write.
		if (auto* writer = this->pendingWriter()) {
			// A queued write is cancelled by the path change: its data is
			// discarded (the target it was meant for is gone). No completion
			// signal is emitted - the write never started and its value never
			// reaches any file.
			writer->disposePending();
			this->pendingOperation = nullptr;
			this->writeData = FileViewData();
		}

		// An empty path unloads the file: updatePath clears the state below,
		// so no queued read is needed (a read of "" would spuriously fail).
		if (!p.isEmpty()) {
			if (auto* reader = this->pendingReader()) {
				// A queued read is reused for the new path (latest read wins).
				reader->state.path = p;
				reader->state.printErrors = this->bPrintErrors;
				this->pathInFlight = p;
			} else {
				auto* queuedReader = new FileViewReader(this, false);
				queuedReader->state.path = p;
				queuedReader->state.printErrors = this->bPrintErrors;
				QObject::connect(
				    queuedReader,
				    &FileViewOperation::done,
				    this,
				    &FileView::operationFinished
				);
				this->pendingOperation = queuedReader;
				this->pathInFlight = p;
			}
		}
	} else {
		this->cancelAsync();
	}

	this->targetPath = p;

	// The path property must reflect the new target immediately, even when a
	// write is still in flight (path() reads state.path). Update only the
	// path - the queued read/write will bring the data once it runs. The
	// previously loaded data and loaded() state stay as they are until the
	// queued operation completes, matching the documented contract: "If a
	// file was loaded, and path was changed to a new file, no blocking will
	// occur" and "text()/data() return the old data until the load
	// completes".
	if (this->state.path != p) {
		this->state.path = p;
		emit this->pathChanged();
	}

	this->updatePath();
}

void FileView::updatePath() {
	this->mPrepared = false;

	if (this->targetPath.isEmpty()) {
		auto state = FileViewState();
		this->updateState(state);
	} else if (this->mPreload) {
		this->loadAsync(true);
	} else {
		this->emitDataChanged();
	}

	this->updateWatchedFiles();
}

void FileView::updateWatchedFiles() {
	// If inotify events are sent to the watcher after deletion and deleteLater
	// isn't used, a use after free in the QML engine will occur.
	if (this->watcher) {
		this->watcher->deleteLater();
		this->watcher = nullptr;
	}

	if (!this->targetPath.isEmpty() && this->bWatchChanges) {
		qCDebug(logFileView) << "Creating watcher for" << this << "at" << this->targetPath;
		this->watcher = new QFileSystemWatcher(this);
		this->watcher->addPath(this->targetPath);

		auto dirPath = this->targetPath;
		if (!dirPath.contains("/")) dirPath = "./" % dirPath;

		if (auto lastIndex = dirPath.lastIndexOf('/'); lastIndex != -1) {
			dirPath = dirPath.sliced(0, lastIndex);
			this->watcher->addPath(dirPath);
		}

		QObject::connect(
		    this->watcher,
		    &QFileSystemWatcher::fileChanged,
		    this,
		    &FileView::onWatchedFileChanged
		);

		QObject::connect(
		    this->watcher,
		    &QFileSystemWatcher::directoryChanged,
		    this,
		    &FileView::onWatchedDirectoryChanged
		);
	}
}

void FileView::onWatchedFileChanged() {
	if (!this->watcher->files().contains(this->targetPath)) {
		this->watcher->addPath(this->targetPath);
	}

	emit this->fileChanged();
}

void FileView::onWatchedDirectoryChanged() {
	if (!this->watcher->files().contains(this->targetPath) && QFileInfo(this->targetPath).exists()) {
		// the file was just created
		this->watcher->addPath(this->targetPath);
		emit this->fileChanged();
	}
}

bool FileView::shouldBlockRead() const {
	return this->mBlockAllReads || (this->mBlockLoading && !this->mLoadedOrAsync);
}

FileViewReader* FileView::liveReader() const {
	return dynamic_cast<FileViewReader*>(this->liveOperation);
}

FileViewWriter* FileView::liveWriter() const {
	return dynamic_cast<FileViewWriter*>(this->liveOperation);
}

FileViewReader* FileView::pendingReader() const {
	return dynamic_cast<FileViewReader*>(this->pendingOperation);
}

FileViewWriter* FileView::pendingWriter() const {
	return dynamic_cast<FileViewWriter*>(this->pendingOperation);
}

const FileViewData& FileView::writeCmpData() const {
	return this->writeData.isEmpty() ? this->state.data : this->writeData;
}

QByteArray FileView::data() {
	auto guard = this->dataChangedEmitter.block();

	if (!this->mPrepared) {
		if (this->shouldBlockRead()) this->loadSync();
		else this->loadAsync(false);
	}

	return this->state.data;
}

QString FileView::text() {
	auto guard = this->textChangedEmitter.block();

	if (!this->mPrepared) {
		if (this->shouldBlockRead()) this->loadSync();
		else this->loadAsync(true);
	}

	return this->state.data;
}

void FileView::setData(const QByteArray& data) {
	if (this->writeCmpData().operator const QByteArray&() == data) return;
	this->writeData = data;

	if (this->bBlockWrites) this->saveSync();
	else this->saveAsync();
}

void FileView::setText(const QString& text) {
	if (this->writeCmpData().operator const QString&() == text) return;
	this->writeData = text;

	if (this->bBlockWrites) this->saveSync();
	else this->saveAsync();
}

void FileView::emitDataChanged() {
	this->dataChangedEmitter.call(this);
	this->textChangedEmitter.call(this);
	emit this->dataChanged();
	emit this->textChanged();
}

DEFINE_MEMBER_GETSET(FileView, isLoadedOrAsync, setLoadedOrAsync);
DEFINE_MEMBER_GET(FileView, shouldPreload);
DEFINE_MEMBER_GET(FileView, blockLoading);
DEFINE_MEMBER_GET(FileView, blockAllReads);

void FileView::setPreload(bool preload) {
	if (preload != this->mPreload) {
		this->mPreload = preload;
		emit this->preloadChanged();

		if (preload) this->emitDataChanged();

		if (!this->mPrepared && this->mPreload) {
			this->loadAsync(false);
		}
	}
}

void FileView::setBlockLoading(bool blockLoading) {
	if (blockLoading != this->mBlockLoading) {
		auto wasBlocking = this->shouldBlockRead();

		this->mBlockLoading = blockLoading;
		emit this->blockLoadingChanged();

		if (!wasBlocking && this->shouldBlockRead()) {
			this->emitDataChanged();
		}
	}
}

void FileView::setBlockAllReads(bool blockAllReads) {
	if (blockAllReads != this->mBlockAllReads) {
		auto wasBlocking = this->shouldBlockRead();

		this->mBlockAllReads = blockAllReads;
		emit this->blockAllReadsChanged();

		if (!wasBlocking && this->shouldBlockRead()) {
			this->emitDataChanged();
		}
	}
}

FileViewAdapter* FileView::adapter() const { return this->mAdapter; }

void FileView::setAdapter(FileViewAdapter* adapter) {
	if (adapter == this->mAdapter) return;

	if (this->mAdapter) {
		this->mAdapter->setFileView(nullptr);
		QObject::disconnect(this->mAdapter, nullptr, this, nullptr);
	}

	this->mAdapter = adapter;

	if (adapter) {
		this->mAdapter->setFileView(this);
		QObject::connect(adapter, &FileViewAdapter::adapterUpdated, this, &FileView::adapterUpdated);
		QObject::connect(adapter, &QObject::destroyed, this, &FileView::onAdapterDestroyed);
	}

	emit this->adapterChanged();
}

void FileView::writeAdapter() {
	if (!this->mAdapter) {
		qmlWarning(this) << "Cannot call writeAdapter without an adapter.";
		return;
	}

	this->setData(this->mAdapter->serializeAdapter());
}

void FileView::onAdapterDestroyed() { this->mAdapter = nullptr; }

void FileViewAdapter::setFileView(FileView* fileView) {
	if (fileView == this->mFileView) return;

	if (this->mFileView) {
		QObject::disconnect(this->mFileView, nullptr, this, nullptr);
	}

	this->mFileView = fileView;

	if (fileView) {
		QObject::connect(fileView, &FileView::dataChanged, this, &FileViewAdapter::onDataChanged);
		this->setFileView(fileView);
	} else {
		this->setFileView(nullptr);
	}
}

void FileViewAdapter::onDataChanged() { this->deserializeAdapter(this->mFileView->data()); }

} // namespace qs::io
