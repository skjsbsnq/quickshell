#pragma once

#include <qfilesystemwatcher.h>
#include <qobject.h>
#include <qstringlist.h>
#include <qtimer.h>

class DesktopEntryMonitor: public QObject {
	Q_OBJECT

public:
	explicit DesktopEntryMonitor(QObject* parent = nullptr);
	~DesktopEntryMonitor() override = default;
	DesktopEntryMonitor(const DesktopEntryMonitor&) = delete;
	DesktopEntryMonitor& operator=(const DesktopEntryMonitor&) = delete;
	DesktopEntryMonitor(DesktopEntryMonitor&&) = delete;
	DesktopEntryMonitor& operator=(DesktopEntryMonitor&&) = delete;

#ifdef QS_TEST
	/// Test-only constructor: watch the given applications roots instead of XDG paths.
	explicit DesktopEntryMonitor(const QStringList& watchRoots, QObject* parent = nullptr);
#endif

signals:
	void desktopEntriesChanged();

private slots:
	void onDirectoryChanged(const QString& path);
	void onFileChanged(const QString& path);
	void processChanges();

private:
	void initCommon();
	void startMonitoring(const QStringList& roots);
	void scanAndWatch(const QString& dirPath);
	void rebuildWatches();

	QFileSystemWatcher watcher;
	QTimer debounceTimer;
	QStringList mRoots;
};
