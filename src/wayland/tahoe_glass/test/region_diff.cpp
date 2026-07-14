#include "region_diff.hpp"

#include <qhash.h>
#include <qlist.h>
#include <qrect.h>
#include <qset.h>
#include <qtest.h>
#include <qtestcase.h>

#include "../surface.hpp"

using qs::wayland::tahoe_glass::impl::canonicalizeRegions;
using qs::wayland::tahoe_glass::impl::diffRegions;
using qs::wayland::tahoe_glass::impl::TahoeGlassRegionDiff;
using qs::wayland::tahoe_glass::impl::TahoeGlassRegionState;

namespace {

TahoeGlassRegionState region(
    quint32 id,
    int x,
    int y,
    int w,
    int h,
    qreal alpha = 1.0,
    const QString& material = QStringLiteral("panel")
) {
	TahoeGlassRegionState state;
	state.id = id;
	state.rect = QRect(x, y, w, h);
	state.material = material;
	state.materialAlpha = alpha;
	return state;
}

bool sameRegion(const TahoeGlassRegionState& lhs, const TahoeGlassRegionState& rhs) {
	return lhs.id == rhs.id && lhs.rect == rhs.rect && lhs.material == rhs.material
	    && lhs.flags == rhs.flags && qFuzzyCompare(1.0 + lhs.interaction, 1.0 + rhs.interaction)
	    && qFuzzyCompare(1.0 + lhs.materialAlpha, 1.0 + rhs.materialAlpha)
	    && lhs.corners.topLeft == rhs.corners.topLeft && lhs.corners.topRight == rhs.corners.topRight
	    && lhs.corners.bottomRight == rhs.corners.bottomRight
	    && lhs.corners.bottomLeft == rhs.corners.bottomLeft;
}

void assertUniqueIds(const QList<TahoeGlassRegionState>& regions) {
	QSet<quint32> seen;
	for (const auto& r: regions) {
		QVERIFY2(!seen.contains(r.id), "canonical regions must not contain duplicate ids");
		seen.insert(r.id);
	}
}

void assertOpsUnique(const TahoeGlassRegionDiff& diff) {
	QSet<quint32> removed;
	for (const auto id: diff.removeIds) {
		QVERIFY2(!removed.contains(id), "each id may be removed at most once");
		removed.insert(id);
	}

	QSet<quint32> setIds;
	for (const auto& r: diff.setRegions) {
		QVERIFY2(!setIds.contains(r.id), "each id may be set at most once");
		setIds.insert(r.id);
	}

	// An id must not be both removed and set in the same diff.
	for (const auto id: setIds) {
		QVERIFY2(!removed.contains(id), "id must not be both removed and set");
	}
}

} // namespace

void TestRegionDiff::canonicalizeLastWins() {
	// A then B share id=1; last-set-wins => B only.
	const auto input = QList {
	    region(1, 0, 0, 10, 10, 0.5),
	    region(1, 5, 5, 20, 20, 1.0),
	};
	const auto canonical = canonicalizeRegions(input);
	QCOMPARE(canonical.size(), 1);
	QVERIFY(sameRegion(canonical.at(0), region(1, 5, 5, 20, 20, 1.0)));
}

void TestRegionDiff::canonicalizePreservesLastOccurrenceOrder() {
	// ids: 1,2,1 final winners are id2 then last id1.
	const auto input = QList {
	    region(1, 0, 0, 1, 1),
	    region(2, 0, 0, 2, 2),
	    region(1, 0, 0, 3, 3),
	};
	const auto canonical = canonicalizeRegions(input);
	QCOMPARE(canonical.size(), 2);
	QCOMPARE(canonical.at(0).id, 2u);
	QCOMPARE(canonical.at(1).id, 1u);
	QCOMPARE(canonical.at(1).rect, QRect(0, 0, 3, 3));
}

void TestRegionDiff::duplicateListSecondSetIsNoop() {
	// Historical bug: old=[A,B] new=[A,B] with same id made sameRegionsById
	// fail, then emit only A, drifting server last-wins from B to A.
	const auto A = region(1, 0, 0, 10, 10, 0.2);
	const auto B = region(1, 1, 1, 20, 20, 0.8);
	const auto list = QList {A, B};

	const auto first = diffRegions({}, list);
	QVERIFY(first.changed);
	QCOMPARE(first.setRegions.size(), 1);
	QVERIFY(sameRegion(first.setRegions.at(0), B));
	assertUniqueIds(first.nextRegions);
	assertOpsUnique(first);
	QCOMPARE(first.nextRegions.size(), 1);
	QVERIFY(sameRegion(first.nextRegions.at(0), B));

	const auto second = diffRegions(first.nextRegions, list);
	QVERIFY2(!second.changed, "identical duplicate-expanded list must be a wire no-op");
	QCOMPARE(second.removeIds.size(), 0);
	QCOMPARE(second.setRegions.size(), 0);
	QVERIFY(!second.clearAll);
	assertUniqueIds(second.nextRegions);
	QVERIFY(sameRegion(second.nextRegions.at(0), B));
}

void TestRegionDiff::duplicateAbFinalIsB() {
	const auto A = region(7, 0, 0, 1, 1, 0.1);
	const auto B = region(7, 9, 9, 4, 4, 0.9);
	const auto diff = diffRegions({}, {A, B});
	QVERIFY(diff.changed);
	QCOMPARE(diff.setRegions.size(), 1);
	QVERIFY(sameRegion(diff.setRegions.at(0), B));
	QCOMPARE(diff.nextRegions.size(), 1);
	QVERIFY(sameRegion(diff.nextRegions.at(0), B));
	assertOpsUnique(diff);
}

void TestRegionDiff::regionIdChangedToExistingId() {
	// old: id1=A, id2=B; new: both claim id1 with last-wins C.
	const auto A = region(1, 0, 0, 1, 1);
	const auto B = region(2, 0, 0, 2, 2);
	const auto C = region(1, 5, 5, 5, 5);
	const auto oldList = QList {A, B};
	const auto newList = QList {A, C}; // id2 gone, id1 updated to C

	const auto diff = diffRegions(oldList, newList);
	QVERIFY(diff.changed);
	assertOpsUnique(diff);
	QCOMPARE(diff.removeIds, QList<quint32> {2});
	QCOMPARE(diff.setRegions.size(), 1);
	QVERIFY(sameRegion(diff.setRegions.at(0), C));
	QCOMPARE(diff.nextRegions.size(), 1);
	QVERIFY(sameRegion(diff.nextRegions.at(0), C));
}

void TestRegionDiff::removeConflictingItem() {
	// old canonical B (from A,B dup); new only A with different content under same id.
	const auto A = region(1, 0, 0, 1, 1, 0.1);
	const auto B = region(1, 2, 2, 2, 2, 0.9);
	const auto first = diffRegions({}, {A, B});
	QVERIFY(sameRegion(first.nextRegions.at(0), B));

	// Drop to a single different region under a new id; old id removed.
	const auto C = region(3, 0, 0, 3, 3);
	const auto diff = diffRegions(first.nextRegions, {C});
	QVERIFY(diff.changed);
	assertOpsUnique(diff);
	QCOMPARE(diff.removeIds, QList<quint32> {1});
	QCOMPARE(diff.setRegions.size(), 1);
	QVERIFY(sameRegion(diff.setRegions.at(0), C));
}

void TestRegionDiff::reorderUniqueIdsIsNoop() {
	const auto A = region(1, 0, 0, 1, 1);
	const auto B = region(2, 0, 0, 2, 2);
	const auto diff = diffRegions({A, B}, {B, A});
	QVERIFY2(!diff.changed, "pure reorder of unique ids must not generate protocol ops");
	QCOMPARE(diff.removeIds.size(), 0);
	QCOMPARE(diff.setRegions.size(), 0);
	// Storage may adopt caller order via nextRegions after canonicalize (unique).
	QCOMPARE(diff.nextRegions.size(), 2);
}

void TestRegionDiff::singleFieldUpdate() {
	const auto A = region(1, 0, 0, 10, 10, 0.5);
	auto B = A;
	B.materialAlpha = 0.25;
	const auto diff = diffRegions({A}, {B});
	QVERIFY(diff.changed);
	QCOMPARE(diff.removeIds.size(), 0);
	QCOMPARE(diff.setRegions.size(), 1);
	QVERIFY(sameRegion(diff.setRegions.at(0), B));
	assertOpsUnique(diff);
}

void TestRegionDiff::clearAll() {
	const auto A = region(1, 0, 0, 1, 1);
	const auto B = region(2, 0, 0, 2, 2);
	const auto diff = diffRegions({A, B}, {});
	QVERIFY(diff.changed);
	QVERIFY(diff.clearAll);
	QCOMPARE(diff.removeIds.size(), 0);
	QCOMPARE(diff.setRegions.size(), 0);
	QCOMPARE(diff.nextRegions.size(), 0);

	const auto empty = diffRegions({}, {});
	QVERIFY(!empty.changed);
	QVERIFY(!empty.clearAll);
}

void TestRegionDiff::eachIdAtMostOneSetOrRemove() {
	// Conflicting duplicates plus an id swap must still emit each id once.
	const auto A1 = region(1, 0, 0, 1, 1);
	const auto A2 = region(1, 1, 1, 2, 2);
	const auto B1 = region(2, 0, 0, 3, 3);
	const auto B2 = region(2, 4, 4, 4, 4);
	const auto C = region(3, 0, 0, 5, 5);

	const auto oldList = QList {A1, B1, A2}; // canonical: B1, A2
	const auto newList = QList {B2, C, B1};  // last B is B1; C new; A gone
	// Wait: newList B2, C, B1 -> last B is B1. Canonical: C, B1. Remove A, set C (B unchanged if B1==B1).

	const auto diff = diffRegions(oldList, newList);
	assertOpsUnique(diff);
	assertUniqueIds(diff.nextRegions);

	// Count ops per id via sets already in assertOpsUnique; also ensure no double sets.
	QHash<quint32, int> setCount;
	for (const auto& r: diff.setRegions) setCount[r.id] += 1;
	for (auto it = setCount.begin(); it != setCount.end(); ++it) {
		QCOMPARE(it.value(), 1);
	}
}

// QTEST_MAIN lives in main.cpp so Task 13/20 can share one executable.
