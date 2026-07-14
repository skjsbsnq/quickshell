#pragma once

#include <memory>

#ifdef QS_TEST
class TestTransformLifecycle;
#endif

#include <private/qquickitemchangelistener_p.h>
#include <private/qwaylandwindow_p.h>
#include <qcoreevent.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qqmllist.h>
#include <qquickitem.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwindow.h>

#include "../../core/region.hpp"
#include "../../window/proxywindow.hpp"
#include "../attached_surface_lifecycle.hpp"
#include "surface.hpp"

namespace qs::wayland::background_effect {
class BackgroundEffect;
}

namespace qs::wayland::tahoe_glass {

// QQuickItemChangeListener covers transform-list (Matrix) updates that have no
// public NOTIFY signal; deliberately specialized for scene AABB, not a second
// TransformWatcher.
class TahoeGlassRegion
    : public QObject
    , public QQuickItemChangeListener {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 regionId READ regionId WRITE setRegionId NOTIFY regionIdChanged);
	Q_PROPERTY(QQuickItem* item READ item WRITE setItem NOTIFY itemChanged);
	Q_PROPERTY(qint32 x READ x WRITE setX NOTIFY xChanged);
	Q_PROPERTY(qint32 y READ y WRITE setY NOTIFY yChanged);
	Q_PROPERTY(qint32 width READ width WRITE setWidth NOTIFY widthChanged);
	Q_PROPERTY(qint32 height READ height WRITE setHeight NOTIFY heightChanged);
	Q_PROPERTY(QString material READ material WRITE setMaterial NOTIFY materialChanged);
	Q_PROPERTY(qint32 radius READ radius WRITE setRadius NOTIFY radiusChanged);
	Q_PROPERTY(qint32 topLeftRadius READ topLeftRadius WRITE setTopLeftRadius RESET resetTopLeftRadius NOTIFY topLeftRadiusChanged);
	Q_PROPERTY(qint32 topRightRadius READ topRightRadius WRITE setTopRightRadius RESET resetTopRightRadius NOTIFY topRightRadiusChanged);
	Q_PROPERTY(qint32 bottomLeftRadius READ bottomLeftRadius WRITE setBottomLeftRadius RESET resetBottomLeftRadius NOTIFY bottomLeftRadiusChanged);
	Q_PROPERTY(qint32 bottomRightRadius READ bottomRightRadius WRITE setBottomRightRadius RESET resetBottomRightRadius NOTIFY bottomRightRadiusChanged);
	Q_PROPERTY(bool blur READ blur WRITE setBlur NOTIFY blurChanged);
	Q_PROPERTY(bool shadow READ shadow WRITE setShadow NOTIFY shadowChanged);
	Q_PROPERTY(bool clip READ clip WRITE setClip NOTIFY clipChanged);
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(qreal interaction READ interaction WRITE setInteraction NOTIFY interactionChanged);
	Q_PROPERTY(qreal materialAlpha READ materialAlpha WRITE setMaterialAlpha NOTIFY materialAlphaChanged);
	// clang-format on

	QML_ELEMENT;

public:
	explicit TahoeGlassRegion(QObject* parent = nullptr);
	~TahoeGlassRegion() override;

	[[nodiscard]] quint32 regionId() const;
	void setRegionId(quint32 id);

	[[nodiscard]] QQuickItem* item() const;
	void setItem(QQuickItem* item);

	[[nodiscard]] qint32 x() const;
	void setX(qint32 x);
	[[nodiscard]] qint32 y() const;
	void setY(qint32 y);
	[[nodiscard]] qint32 width() const;
	void setWidth(qint32 width);
	[[nodiscard]] qint32 height() const;
	void setHeight(qint32 height);

	[[nodiscard]] QString material() const;
	void setMaterial(const QString& material);

	[[nodiscard]] qint32 radius() const;
	void setRadius(qint32 radius);
	[[nodiscard]] qint32 topLeftRadius() const;
	void setTopLeftRadius(qint32 radius);
	void resetTopLeftRadius();
	[[nodiscard]] qint32 topRightRadius() const;
	void setTopRightRadius(qint32 radius);
	void resetTopRightRadius();
	[[nodiscard]] qint32 bottomLeftRadius() const;
	void setBottomLeftRadius(qint32 radius);
	void resetBottomLeftRadius();
	[[nodiscard]] qint32 bottomRightRadius() const;
	void setBottomRightRadius(qint32 radius);
	void resetBottomRightRadius();

	[[nodiscard]] bool blur() const;
	void setBlur(bool blur);
	[[nodiscard]] bool shadow() const;
	void setShadow(bool shadow);
	[[nodiscard]] bool clip() const;
	void setClip(bool clip);
	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

		[[nodiscard]] qreal interaction() const;
		void setInteraction(qreal interaction);
		[[nodiscard]] qreal materialAlpha() const;
		void setMaterialAlpha(qreal materialAlpha);

		[[nodiscard]] bool buildLogicalRegion(impl::TahoeGlassRegionState* state) const;

	[[nodiscard]] bool buildSurfaceRegion(
	    QtWaylandClient::QWaylandWindow* waylandWindow,
	    impl::TahoeGlassRegionState* state
	) const;

	/// Axis-aligned scene bounds of an item after full scene transform.
	/// Maps all four local corners (not only the diagonal) so rotation/scale
	/// with non-default transform origins produce a correct AABB.
	[[nodiscard]] static QRectF itemSceneBounds(const QQuickItem* item);

signals:
	void regionIdChanged();
	void itemChanged();
	void xChanged();
	void yChanged();
	void widthChanged();
	void heightChanged();
	void materialChanged();
	void radiusChanged();
	void topLeftRadiusChanged();
	void topRightRadiusChanged();
	void bottomLeftRadiusChanged();
	void bottomRightRadiusChanged();
	void blurChanged();
	void shadowChanged();
	void clipChanged();
	void enabledChanged();
	void interactionChanged();
	void materialAlphaChanged();
	void changed();

private slots:
	void onItemGeometryChanged();
	void onItemAncestryChanged();
	void onTrackedItemDestroyed();

private:
	enum CornerOverride : quint8 {
		TopLeft = 0b1,
		TopRight = 0b10,
		BottomLeft = 0b100,
		BottomRight = 0b1000,
	};

	// QQuickItemChangeListener: transform: [...] list / matrix updates.
	// Destroy cleanup is solely via QObject::destroyed (pointer identity),
	// not ChangeListener::itemDestroyed — only Matrix is registered.
	void itemTransformChanged(QQuickItem* item, QQuickItem* transformedItem) override;

	[[nodiscard]] bool buildRegion(impl::TahoeGlassRegionState* state) const;
	/// Unlink all tracked items. If `dying` is set, skip QQuickItemPrivate
	/// access on that object (it is mid-destruction; listeners die with it).
	void unlinkTrackedItems(QObject* dying = nullptr);
	void linkTrackedItems(QQuickItem* item, QObject* skipItem = nullptr);
	void linkTrackedItem(QQuickItem* item);

#ifdef QS_TEST
	// Test-only observation seam (not QML-facing). Production builds omit this.
	friend class ::TestTransformLifecycle;
	[[nodiscard]] QList<QQuickItem*> trackedItemsForTest() const { return this->mTrackedItems; }
	void linkTrackedItemsForTest(QQuickItem* item, QObject* skipItem) {
		this->linkTrackedItems(item, skipItem);
	}
#endif

	quint32 mRegionId = 0;
	QQuickItem* mItem = nullptr;
	QList<QQuickItem*> mTrackedItems;
	qint32 mX = 0;
	qint32 mY = 0;
	qint32 mWidth = 0;
	qint32 mHeight = 0;
	QString mMaterial = QStringLiteral("panel");
	qint32 mRadius = 0;
	qint32 mTopLeftRadius = 0;
	qint32 mTopRightRadius = 0;
	qint32 mBottomLeftRadius = 0;
	qint32 mBottomRightRadius = 0;
	quint8 mCornerOverrides = 0;
	bool mBlur = true;
	bool mShadow = true;
	bool mClip = true;
	bool mEnabled = true;
	qreal mInteraction = 0.0;
	qreal mMaterialAlpha = 1.0;
};


class TahoeGlass: public AttachedSurfaceLifecycle {
	Q_OBJECT;
	Q_PROPERTY(QQmlListProperty<TahoeGlassRegion> regions READ regions NOTIFY regionsChanged);
	Q_PROPERTY(bool available READ available NOTIFY availableChanged);
	Q_PROPERTY(bool fallbackEnabled READ fallbackEnabled WRITE setFallbackEnabled NOTIFY fallbackEnabledChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("TahoeGlass can only be used as an attached object.");
	QML_ATTACHED(TahoeGlass);

public:
	explicit TahoeGlass(ProxyWindowBase* window);

	QQmlListProperty<TahoeGlassRegion> regions();
	[[nodiscard]] bool available() const;
	[[nodiscard]] bool fallbackEnabled() const;
	void setFallbackEnabled(bool enabled);

	static TahoeGlass* qmlAttachedProperties(QObject* object);

signals:
	void regionsChanged();
	void availableChanged();
	void fallbackEnabledChanged();

private slots:
	void onRegionDestroyed();
	void updateRegions();
	void onWindowPolished();

private:
	static void regionsAppend(QQmlListProperty<TahoeGlassRegion>* prop, TahoeGlassRegion* region);
	static TahoeGlassRegion* regionAt(QQmlListProperty<TahoeGlassRegion>* prop, qsizetype i);
	static void regionsClear(QQmlListProperty<TahoeGlassRegion>* prop);
	static qsizetype regionsCount(QQmlListProperty<TahoeGlassRegion>* prop);
	static void regionsRemoveLast(QQmlListProperty<TahoeGlassRegion>* prop);
	static void
	regionsReplace(QQmlListProperty<TahoeGlassRegion>* prop, qsizetype i, TahoeGlassRegion* region);

	void setAvailable(bool available);
	void clearFallback();
	void updateFallback(const QList<impl::TahoeGlassRegionState>& regions);

	void backingWindowConnected() override;
	void platformSurfaceAboutToBeDestroyed() override;
	void waylandWindowDestroyed() override;
	void waylandSurfaceCreated() override;
	void waylandSurfaceDestroyed() override;
	void proxyWindowDestroyed() override;
	bool filteredWindowEvent(QObject* object, QEvent* event) override;

	bool pendingRegions = false;
	bool mAvailable = false;
	bool mFallbackEnabled = true;
	QList<TahoeGlassRegion*> mRegions;
	std::unique_ptr<impl::TahoeGlassSurface> surface;
	background_effect::BackgroundEffect* fallbackEffect = nullptr;
	PendingRegion* fallbackRegion = nullptr;
};

} // namespace qs::wayland::tahoe_glass
