#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestDesktopEntryMonitor: public QObject {
	Q_OBJECT;

private slots:
	void createDesktopFileEmitsChange();
	void inPlaceContentModifyEmitsChange();
	void atomicRenameOverwriteEmitsChange();
	void deleteDesktopFileEmitsChange();
	void newSubdirThenFileEmitsChange();
	void rapidEventsDebounceToOneSignal();
	void watcherStillWorksAfterRescan();
};
