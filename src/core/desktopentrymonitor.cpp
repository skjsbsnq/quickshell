#include "desktopentrymonitor.hpp"

#include <qdir.h>
#include <qfileinfo.h>
#include <qfilesystemwatcher.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "desktopentry.hpp"

namespace {
void addPathAndParents(QFileSystemWatcher& watcher, const QString& path) {
	watcher.addPath(path);

	auto p = QFileInfo(path).absolutePath();
	while (!p.isEmpty()) {
		watcher.addPath(p);
		const auto parent = QFileInfo(p).dir().absolutePath();
		if (parent == p) break;
		p = parent;
	}
}

void watchDesktopFilesInDir(QFileSystemWatcher& watcher, const QDir& dir) {
	const auto files = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
	for (const auto& file: files) {
		watcher.addPath(file.absoluteFilePath());
	}
}
} // namespace

void DesktopEntryMonitor::initCommon() {
	this->debounceTimer.setSingleShot(true);
	this->debounceTimer.setInterval(100);

	QObject::connect(
	    &this->watcher,
	    &QFileSystemWatcher::directoryChanged,
	    this,
	    &DesktopEntryMonitor::onDirectoryChanged
	);
	QObject::connect(
	    &this->watcher,
	    &QFileSystemWatcher::fileChanged,
	    this,
	    &DesktopEntryMonitor::onFileChanged
	);
	QObject::connect(
	    &this->debounceTimer,
	    &QTimer::timeout,
	    this,
	    &DesktopEntryMonitor::processChanges
	);
}

DesktopEntryMonitor::DesktopEntryMonitor(QObject* parent): QObject(parent) {
	this->initCommon();
	this->startMonitoring(DesktopEntryManager::desktopPaths());
}

#ifdef QS_TEST
DesktopEntryMonitor::DesktopEntryMonitor(const QStringList& watchRoots, QObject* parent)
    : QObject(parent) {
	this->initCommon();
	this->startMonitoring(watchRoots);
}
#endif

void DesktopEntryMonitor::startMonitoring(const QStringList& roots) {
	this->mRoots = roots;
	this->rebuildWatches();
}

void DesktopEntryMonitor::rebuildWatches() {
	for (const auto& path: this->mRoots) {
		if (!QDir(path).exists()) continue;
		addPathAndParents(this->watcher, path);
		this->scanAndWatch(path);
	}
}

void DesktopEntryMonitor::scanAndWatch(const QString& dirPath) {
	auto dir = QDir(dirPath);
	if (!dir.exists()) return;

	this->watcher.addPath(dirPath);
	watchDesktopFilesInDir(this->watcher, dir);

	// Match the historical one-level subdirectory depth, but also watch desktop
	// files inside those subdirectories so in-place edits are observed.
	const auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
	for (const auto& subdir: subdirs) {
		const auto subPath = subdir.absoluteFilePath();
		this->watcher.addPath(subPath);
		watchDesktopFilesInDir(this->watcher, QDir(subPath));
	}
}

void DesktopEntryMonitor::onDirectoryChanged(const QString& /*path*/) {
	this->debounceTimer.start();
}

void DesktopEntryMonitor::onFileChanged(const QString& /*path*/) {
	// In-place content edits and atomic replacements surface here. After the
	// debounce, rebuildWatches re-adds paths that the kernel dropped.
	this->debounceTimer.start();
}

void DesktopEntryMonitor::processChanges() {
	// Atomic replace / delete often auto-removes the file path from the watcher.
	// Rescan roots so the next edit is still observed, then notify the manager.
	this->rebuildWatches();
	emit this->desktopEntriesChanged();
}
