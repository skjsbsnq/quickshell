#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class QTemporaryFile;

class TestColorQuantizer: public QObject {
	Q_OBJECT;

private slots:
	void initTestCase();
	void cancelAsyncDoesNotBlockGuiThread();
	void destroyWhileOperationQueued();
	void cleanupTestCase();

private:
	QTemporaryFile* redImage = nullptr;
	QTemporaryFile* blueImage = nullptr;
};
