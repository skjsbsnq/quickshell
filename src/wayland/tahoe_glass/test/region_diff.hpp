#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestRegionDiff: public QObject {
	Q_OBJECT;

private slots:
	void canonicalizeLastWins();
	void canonicalizePreservesLastOccurrenceOrder();
	void duplicateListSecondSetIsNoop();
	void duplicateAbFinalIsB();
	void regionIdChangedToExistingId();
	void removeConflictingItem();
	void reorderUniqueIdsIsNoop();
	void singleFieldUpdate();
	void clearAll();
	void eachIdAtMostOneSetOrRemove();
};
