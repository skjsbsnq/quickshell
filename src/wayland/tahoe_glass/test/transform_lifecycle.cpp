#include "transform_lifecycle.hpp"

#include <qguiapplication.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qsignalspy.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../qml.hpp"

using qs::wayland::tahoe_glass::TahoeGlassRegion;

// Friend helpers must be members of TestTransformLifecycle (friend declaration).
int TestTransformLifecycle::countTracked(const TahoeGlassRegion& region, QQuickItem* item) {
	int n = 0;
	for (auto* tracked: region.trackedItemsForTest()) {
		if (tracked == item) n += 1;
	}
	return n;
}

bool TestTransformLifecycle::containsTracked(const TahoeGlassRegion& region, QQuickItem* item) {
	return countTracked(region, item) > 0;
}

void TestTransformLifecycle::itemDestroyClearsTracking() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* item = new QQuickItem(root);
	item->setSize(QSizeF(40, 40));

	TahoeGlassRegion region;
	region.setItem(item);
	QVERIFY(containsTracked(region, item));
	QVERIFY(containsTracked(region, root));

	QSignalSpy itemSpy(&region, &TahoeGlassRegion::itemChanged);
	delete item;
	QTRY_COMPARE(itemSpy.count(), 1);
	QCOMPARE(region.item(), nullptr);
	QVERIFY(region.trackedItemsForTest().isEmpty());

	delete window;
}

void TestTransformLifecycle::directParentDestroyRewires() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* parent = new QQuickItem(root);
	auto* child = new QQuickItem(parent);
	child->setSize(QSizeF(20, 20));

	TahoeGlassRegion region;
	region.setItem(child);
	QVERIFY(containsTracked(region, child));
	QVERIFY(containsTracked(region, parent));
	QVERIFY(containsTracked(region, root));

	delete parent;
	QTRY_COMPARE(region.item(), nullptr);
	QVERIFY(region.trackedItemsForTest().isEmpty());

	delete window;
}

void TestTransformLifecycle::visualParentDestroyWhileChildSurvives() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* visualParent = new QQuickItem(root);
	// Child QObject-owned by root, visual-parented to visualParent.
	auto* child = new QQuickItem(root);
	child->setParentItem(visualParent);
	child->setSize(QSizeF(16, 16));

	TahoeGlassRegion region;
	region.setItem(child);
	QVERIFY(containsTracked(region, child));
	QVERIFY(containsTracked(region, visualParent));
	QVERIFY(containsTracked(region, root));

	// Model "visual parent gone, child QObject still alive".
	child->setParentItem(root);
	delete visualParent;
	QTest::qWait(0);

	QCOMPARE(region.item(), child);
	QVERIFY(containsTracked(region, child));
	QVERIFY(containsTracked(region, root));
	QVERIFY(!containsTracked(region, visualParent));

	delete window;
}

void TestTransformLifecycle::reparentDoesNotDuplicateListeners() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* a = new QQuickItem(root);
	auto* b = new QQuickItem(root);
	auto* child = new QQuickItem(a);
	child->setSize(QSizeF(10, 10));

	TahoeGlassRegion region;
	region.setItem(child);
	QCOMPARE(countTracked(region, child), 1);
	QCOMPARE(countTracked(region, a), 1);

	child->setParentItem(b);
	QTest::qWait(0);
	QCOMPARE(countTracked(region, child), 1);
	QCOMPARE(countTracked(region, b), 1);
	QVERIFY(!containsTracked(region, a));

	child->setParentItem(a);
	QTest::qWait(0);
	QCOMPARE(countTracked(region, child), 1);
	QCOMPARE(countTracked(region, a), 1);
	QVERIFY(!containsTracked(region, b));

	delete window;
}

void TestTransformLifecycle::repeatedReparentStableChanged() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* a = new QQuickItem(root);
	auto* b = new QQuickItem(root);
	auto* child = new QQuickItem(a);
	child->setSize(QSizeF(12, 12));

	TahoeGlassRegion region;
	region.setItem(child);
	QSignalSpy spy(&region, &TahoeGlassRegion::changed);
	const auto before = spy.count();

	for (int i = 0; i < 8; ++i) {
		child->setParentItem(i % 2 == 0 ? b : a);
		QTest::qWait(0);
	}

	QVERIFY(spy.count() >= before);
	QCOMPARE(countTracked(region, child), 1);
	QCOMPARE(countTracked(region, a), 1);
	QVERIFY(!containsTracked(region, b));

	delete window;
}

void TestTransformLifecycle::regionDestroyBeforeItem() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* item = new QQuickItem(root);
	item->setSize(QSizeF(30, 30));

	auto* region = new TahoeGlassRegion();
	region->setItem(item);
	QVERIFY(containsTracked(*region, item));

	delete region;
	item->setX(3);
	item->setScale(1.2);
	delete item;
	delete window;
	QVERIFY(true);
}

void TestTransformLifecycle::skipItemBreaksTraversalWithoutParentCall() {
	auto* window = new QQuickWindow();
	window->resize(200, 200);
	window->show();
	QVERIFY(QTest::qWaitForWindowExposed(window));

	auto* root = window->contentItem();
	auto* mid = new QQuickItem(root);
	auto* leaf = new QQuickItem(mid);
	leaf->setSize(QSizeF(8, 8));

	TahoeGlassRegion region;
	region.setItem(leaf);
	QVERIFY(containsTracked(region, leaf));
	QVERIFY(containsTracked(region, mid));
	QVERIFY(containsTracked(region, root));

	// skipItem=mid must break before parentItem(mid); only leaf remains tracked.
	region.linkTrackedItemsForTest(leaf, mid);
	QVERIFY(containsTracked(region, leaf));
	QVERIFY(!containsTracked(region, mid));
	QCOMPARE(region.trackedItemsForTest().size(), 1);
	QCOMPARE(region.trackedItemsForTest().first(), leaf);

	delete window;
}
