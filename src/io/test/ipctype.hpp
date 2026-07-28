#pragma once

#include <qcolor.h>
#include <qobject.h>
#include <qtmetamacros.h>

// Probe object for color parameter / return / property IPC paths.
class ColorIpcProbe: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged);

public:
	explicit ColorIpcProbe(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QColor color() const { return this->mColor; }

	void setColor(const QColor& color) {
		if (this->mColor == color) return;
		this->mColor = color;
		emit this->colorChanged();
	}

	// Parameter + return in one call: identity round-trip through IPC storage.
	Q_INVOKABLE QColor identity(QColor value) { return value; }

	Q_INVOKABLE QColor getColor() const { return this->mColor; }

	Q_INVOKABLE void setColorInvokable(QColor value) { this->setColor(value); }

signals:
	void colorChanged();

private:
	QColor mColor {Qt::red};
};

class TestIpcType: public QObject {
	Q_OBJECT;

private slots:
	// Storage layer invariants for every registered value type.
	void storageSizeMatchesType();
	void storageCreateDestroyRoundtrip();
	void storageCopyIsDeep();

	// Color-specific wire format.
	void colorFromStringVariants();
	void colorFromStringRejectsInvalid();
	void colorToStringHexArgb();

	// Three IPC paths required by T-05 / F-01 acceptance.
	void colorParameterPath();   // fromString → invoke arg
	void colorReturnPath();      // createStorage → QMetaMethod return write
	void colorPropertyPath();    // QVariant read → copyStorage
	void colorIdentityRoundtrip(); // param + return together
};
