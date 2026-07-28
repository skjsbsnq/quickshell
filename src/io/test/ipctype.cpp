#include "ipctype.hpp"

#include <cstring>

#include <qcolor.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qtest.h>
#include <qtestcase.h>
#include <qvariant.h>

#include "../ipc.hpp"
#include "../ipchandler.hpp"

using namespace qs::io::ipc;

namespace {

void assertStorageMatchesType(const IpcType* type, qsizetype expectedSize) {
	QVERIFY(type != nullptr);
	QCOMPARE(type->size(), expectedSize);

	void* storage = type->createStorage();
	QVERIFY(storage != nullptr);

	// Touch the last allocated byte so ASan flags undersized createStorage
	// without clobbering non-trivial object representation (QString/QColor).
	if (type->size() > 0) {
		static_cast<unsigned char*>(storage)[static_cast<size_t>(type->size()) - 1] ^= 0;
	}

	type->destroyStorage(storage);
}

QMetaMethod findMethod(const QObject* object, const char* name) {
	const auto* meta = object->metaObject();
	for (auto i = 0; i < meta->methodCount(); i++) {
		auto method = meta->method(i);
		if (method.name() == name) return method;
	}
	return {};
}

} // namespace

void TestIpcType::storageSizeMatchesType() {
	QCOMPARE(VoidIpcType::INSTANCE.size(), 0);
	QCOMPARE(StringIpcType::INSTANCE.size(), static_cast<qsizetype>(sizeof(QString)));
	QCOMPARE(IntIpcType::INSTANCE.size(), static_cast<qsizetype>(sizeof(int)));
	QCOMPARE(BoolIpcType::INSTANCE.size(), static_cast<qsizetype>(sizeof(bool)));
	QCOMPARE(DoubleIpcType::INSTANCE.size(), static_cast<qsizetype>(sizeof(double)));
	QCOMPARE(ColorIpcType::INSTANCE.size(), static_cast<qsizetype>(sizeof(QColor)));

	// Registry must resolve QColor to the color IPC type (publicly supported).
	QCOMPARE(IpcType::ipcType(QMetaType(QMetaType::QColor)), &ColorIpcType::INSTANCE);
}

void TestIpcType::storageCreateDestroyRoundtrip() {
	assertStorageMatchesType(&StringIpcType::INSTANCE, sizeof(QString));
	assertStorageMatchesType(&IntIpcType::INSTANCE, sizeof(int));
	assertStorageMatchesType(&BoolIpcType::INSTANCE, sizeof(bool));
	assertStorageMatchesType(&DoubleIpcType::INSTANCE, sizeof(double));
	assertStorageMatchesType(&ColorIpcType::INSTANCE, sizeof(QColor));
}

void TestIpcType::storageCopyIsDeep() {
	// QString: memcpy-based copy would share/corrupt refcount; typed copy must own.
	{
		const auto original = QString("hello-ipc");
		void* copied = StringIpcType::INSTANCE.copyStorage(&original);
		QVERIFY(copied != nullptr);
		QCOMPARE(*static_cast<QString*>(copied), original);
		// Mutate original; copy must remain stable.
		const_cast<QString&>(original).append("-mutated");
		QCOMPARE(*static_cast<QString*>(copied), QString("hello-ipc"));
		StringIpcType::INSTANCE.destroyStorage(copied);
	}

	// QColor: the F-01 failure mode was createStorage allocating bool while size
	// claimed sizeof(QColor). Typed copy + destroy must be consistent.
	{
		const auto original = QColor(0x12, 0x34, 0x56, 0x78);
		void* copied = ColorIpcType::INSTANCE.copyStorage(&original);
		QVERIFY(copied != nullptr);
		QCOMPARE(*static_cast<QColor*>(copied), original);
		ColorIpcType::INSTANCE.destroyStorage(copied);
	}
}

void TestIpcType::colorFromStringVariants() {
	struct Case {
		const char* input;
		QColor expected;
	};

	const Case cases[] = {
	    {"#ff0000", QColor(255, 0, 0)},
	    {"ff0000", QColor(255, 0, 0)},
	    {"#80ff0000", QColor(255, 0, 0, 0x80)},
	    {"red", QColor(Qt::red)},
	    {"#00ff00", QColor(0, 255, 0)},
	};

	for (const auto& c: cases) {
		void* slot = ColorIpcType::INSTANCE.fromString(QString::fromUtf8(c.input));
		QVERIFY2(slot != nullptr, c.input);
		QCOMPARE(*static_cast<QColor*>(slot), c.expected);
		ColorIpcType::INSTANCE.destroyStorage(slot);
	}
}

void TestIpcType::colorFromStringRejectsInvalid() {
	void* slot = ColorIpcType::INSTANCE.fromString("not-a-color-xyz");
	QCOMPARE(slot, nullptr);
}

void TestIpcType::colorToStringHexArgb() {
	auto color = QColor(0x11, 0x22, 0x33, 0x44);
	void* slot = new QColor(color);
	QCOMPARE(ColorIpcType::INSTANCE.toString(slot), QString("#44112233"));
	ColorIpcType::INSTANCE.destroyStorage(slot);
}

void TestIpcType::colorParameterPath() {
	// Parameter path: wire string → fromString → IpcCallStorage argument slot → invoke.
	ColorIpcProbe probe;
	probe.setColor(Qt::black);

	auto method = findMethod(&probe, "setColorInvokable");
	QVERIFY(method.isValid());

	IpcFunction func(method);
	QString error;
	QVERIFY2(func.resolve(error), qPrintable(error));
	QCOMPARE(func.argumentTypes.size(), 1);
	QCOMPARE(func.argumentTypes.at(0), &ColorIpcType::INSTANCE);
	QCOMPARE(func.returnType, &VoidIpcType::INSTANCE);

	IpcCallStorage storage(func);
	QVERIFY(storage.setArgumentStr(0, "#ff00aa"));
	func.invoke(&probe, storage);

	QCOMPARE(probe.color(), QColor(0xff, 0x00, 0xaa));
}

void TestIpcType::colorReturnPath() {
	// Return path: createStorage(sizeof QColor) → QMetaMethod writes QColor into slot.
	ColorIpcProbe probe;
	probe.setColor(QColor(0xab, 0xcd, 0xef, 0x12));

	auto method = findMethod(&probe, "getColor");
	QVERIFY(method.isValid());

	IpcFunction func(method);
	QString error;
	QVERIFY2(func.resolve(error), qPrintable(error));
	QCOMPARE(func.returnType, &ColorIpcType::INSTANCE);

	IpcCallStorage storage(func);
	func.invoke(&probe, storage);

	// getReturnStr → toString on the return slot; must not overrun a bool-sized buffer.
	QCOMPARE(storage.getReturnStr(), QString("#12abcdef"));
}

void TestIpcType::colorPropertyPath() {
	// Property path: QMetaProperty::read → QVariant → copyStorage(sizeof QColor).
	ColorIpcProbe probe;
	probe.setColor(QColor(0x10, 0x20, 0x30, 0x40));

	const auto* meta = probe.metaObject();
	const auto propIndex = meta->indexOfProperty("color");
	QVERIFY(propIndex >= 0);
	IpcProperty ipcProp(meta->property(propIndex));
	QString error;
	QVERIFY2(ipcProp.resolve(error), qPrintable(error));
	QCOMPARE(ipcProp.type, &ColorIpcType::INSTANCE);

	IpcTypeSlot slot(ipcProp.type);
	ipcProp.read(&probe, slot);

	QCOMPARE(slot.type()->toString(slot.get()), QString("#40102030"));
	QCOMPARE(*static_cast<QColor*>(slot.get()), QColor(0x10, 0x20, 0x30, 0x40));
}

void TestIpcType::colorIdentityRoundtrip() {
	// Combined parameter + return: the highest-stress path for storage sizing.
	ColorIpcProbe probe;

	auto method = findMethod(&probe, "identity");
	QVERIFY(method.isValid());

	IpcFunction func(method);
	QString error;
	QVERIFY2(func.resolve(error), qPrintable(error));
	QCOMPARE(func.argumentTypes.at(0), &ColorIpcType::INSTANCE);
	QCOMPARE(func.returnType, &ColorIpcType::INSTANCE);

	IpcCallStorage storage(func);
	QVERIFY(storage.setArgumentStr(0, "orange"));
	func.invoke(&probe, storage);

	auto expected = QColor(QStringLiteral("orange"));
	QCOMPARE(storage.getReturnStr(), expected.name(QColor::HexArgb));
}

QTEST_MAIN(TestIpcType);
