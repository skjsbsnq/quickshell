#pragma once

#include <qobject.h>
#include <qquickitem.h>
#include <qtmetamacros.h>

namespace qs::wayland::tahoe_glass {
class TahoeGlassRegion;
}

class TestTransformLifecycle: public QObject {
	Q_OBJECT;

private:
	static int
	countTracked(const qs::wayland::tahoe_glass::TahoeGlassRegion& region, QQuickItem* item);
	static bool
	containsTracked(const qs::wayland::tahoe_glass::TahoeGlassRegion& region, QQuickItem* item);

private slots:
	void itemDestroyClearsTracking();
	void directParentDestroyRewires();
	void visualParentDestroyWhileChildSurvives();
	void reparentDoesNotDuplicateListeners();
	void repeatedReparentStableChanged();
	void regionDestroyBeforeItem();
	void skipItemBreaksTraversalWithoutParentCall();
};
