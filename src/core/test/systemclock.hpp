#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestSystemClock: public QObject {
	Q_OBJECT;

private slots:
	void initialValue();
	void explicitResync();
	void forwardJumpConvergesOnTimeout();
	void backwardJumpConvergesOnTimeout();
	void timezoneOffsetChange();
	void disableEnable();
	void targetSkewWithin500msUsesTarget();
	void targetSkewBeyond500msUsesWallClock();
	void resyncWhileDisabledStillUpdatesDate();
	void noUpdateNowOrRefreshAliases();
};
