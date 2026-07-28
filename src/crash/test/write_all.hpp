#pragma once

#include <qobject.h>
#include <qtmetamacros.h>

class TestWriteAll: public QObject {
	Q_OBJECT;

private slots:
	void fullWriteUncapped();
	void partialChunksNoDuplication();
	void partialChunksVaryingSizes();
	void multiFrameSequence();
	void hardErrorReturnsFalse();
	void smallPipeBufferNoDuplication();
};
