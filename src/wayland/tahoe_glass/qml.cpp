#include "qml.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include <private/qwaylandwindow_p.h>
#include <qcoreevent.h>
#include <qevent.h>
#include <qglobal.h>
#include <qlogging.h>
#include <qobject.h>
#include <qpoint.h>
#include <qqml.h>
#include <qqmllist.h>
#include <qquickitem.h>
#include <qrect.h>
#include <qtmetamacros.h>
#include <qvariant.h>
#include <qwindow.h>

#include "../../core/region.hpp"
#include "../../window/proxywindow.hpp"
#include "../../window/windowinterface.hpp"
#include "../background_effect/qml.hpp"
#include "manager.hpp"
#include "surface.hpp"

using QtWaylandClient::QWaylandWindow;

namespace qs::wayland::tahoe_glass {

namespace {

quint32 nextRegionId() {
	static quint32 next = 1;
	return next++;
}

} // namespace

TahoeGlassRegion::TahoeGlassRegion(QObject* parent): QObject(parent), mRegionId(nextRegionId()) {
	QObject::connect(this, &TahoeGlassRegion::regionIdChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::itemChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::xChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::yChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::widthChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::heightChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::materialChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::radiusChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::topLeftRadiusChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::topRightRadiusChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::bottomLeftRadiusChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::bottomRightRadiusChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::blurChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::shadowChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::clipChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::enabledChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::interactionChanged, this, &TahoeGlassRegion::changed);
	QObject::connect(this, &TahoeGlassRegion::materialAlphaChanged, this, &TahoeGlassRegion::changed);
}

quint32 TahoeGlassRegion::regionId() const { return this->mRegionId; }

void TahoeGlassRegion::setRegionId(quint32 id) {
	if (id == this->mRegionId) return;
	this->mRegionId = id;
	emit this->regionIdChanged();
}

QQuickItem* TahoeGlassRegion::item() const { return this->mItem; }

void TahoeGlassRegion::setItem(QQuickItem* item) {
	if (item == this->mItem) return;

	if (this->mItem != nullptr) {
		QObject::disconnect(this->mItem, nullptr, this, nullptr);
	}

	this->mItem = item;

	if (item != nullptr) {
		QObject::connect(item, &QObject::destroyed, this, &TahoeGlassRegion::onItemDestroyed);
		QObject::connect(item, &QQuickItem::xChanged, this, &TahoeGlassRegion::onItemGeometryChanged);
		QObject::connect(item, &QQuickItem::yChanged, this, &TahoeGlassRegion::onItemGeometryChanged);
		QObject::connect(item, &QQuickItem::widthChanged, this, &TahoeGlassRegion::onItemGeometryChanged);
		QObject::connect(item, &QQuickItem::heightChanged, this, &TahoeGlassRegion::onItemGeometryChanged);
		QObject::connect(item, &QQuickItem::visibleChanged, this, &TahoeGlassRegion::onItemGeometryChanged);
	}

	emit this->itemChanged();
}

qint32 TahoeGlassRegion::x() const { return this->mX; }

void TahoeGlassRegion::setX(qint32 x) {
	if (x == this->mX) return;
	this->mX = x;
	emit this->xChanged();
}

qint32 TahoeGlassRegion::y() const { return this->mY; }

void TahoeGlassRegion::setY(qint32 y) {
	if (y == this->mY) return;
	this->mY = y;
	emit this->yChanged();
}

qint32 TahoeGlassRegion::width() const { return this->mWidth; }

void TahoeGlassRegion::setWidth(qint32 width) {
	if (width == this->mWidth) return;
	this->mWidth = width;
	emit this->widthChanged();
}

qint32 TahoeGlassRegion::height() const { return this->mHeight; }

void TahoeGlassRegion::setHeight(qint32 height) {
	if (height == this->mHeight) return;
	this->mHeight = height;
	emit this->heightChanged();
}

QString TahoeGlassRegion::material() const { return this->mMaterial; }

void TahoeGlassRegion::setMaterial(const QString& material) {
	auto normalized = material.isEmpty() ? QStringLiteral("panel") : material;
	if (normalized == this->mMaterial) return;
	this->mMaterial = normalized;
	emit this->materialChanged();
}

qint32 TahoeGlassRegion::radius() const { return this->mRadius; }

void TahoeGlassRegion::setRadius(qint32 radius) {
	if (radius == this->mRadius) return;
	this->mRadius = radius;
	emit this->radiusChanged();

	if (!(this->mCornerOverrides & TopLeft)) emit this->topLeftRadiusChanged();
	if (!(this->mCornerOverrides & TopRight)) emit this->topRightRadiusChanged();
	if (!(this->mCornerOverrides & BottomLeft)) emit this->bottomLeftRadiusChanged();
	if (!(this->mCornerOverrides & BottomRight)) emit this->bottomRightRadiusChanged();
}

qint32 TahoeGlassRegion::topLeftRadius() const {
	return (this->mCornerOverrides & TopLeft) ? this->mTopLeftRadius : this->mRadius;
}

void TahoeGlassRegion::setTopLeftRadius(qint32 radius) {
	if ((this->mCornerOverrides & TopLeft) && radius == this->mTopLeftRadius) return;
	this->mTopLeftRadius = radius;
	this->mCornerOverrides |= TopLeft;
	emit this->topLeftRadiusChanged();
}

void TahoeGlassRegion::resetTopLeftRadius() {
	if (!(this->mCornerOverrides & TopLeft)) return;
	this->mCornerOverrides &= ~TopLeft;
	emit this->topLeftRadiusChanged();
}

qint32 TahoeGlassRegion::topRightRadius() const {
	return (this->mCornerOverrides & TopRight) ? this->mTopRightRadius : this->mRadius;
}

void TahoeGlassRegion::setTopRightRadius(qint32 radius) {
	if ((this->mCornerOverrides & TopRight) && radius == this->mTopRightRadius) return;
	this->mTopRightRadius = radius;
	this->mCornerOverrides |= TopRight;
	emit this->topRightRadiusChanged();
}

void TahoeGlassRegion::resetTopRightRadius() {
	if (!(this->mCornerOverrides & TopRight)) return;
	this->mCornerOverrides &= ~TopRight;
	emit this->topRightRadiusChanged();
}

qint32 TahoeGlassRegion::bottomLeftRadius() const {
	return (this->mCornerOverrides & BottomLeft) ? this->mBottomLeftRadius : this->mRadius;
}

void TahoeGlassRegion::setBottomLeftRadius(qint32 radius) {
	if ((this->mCornerOverrides & BottomLeft) && radius == this->mBottomLeftRadius) return;
	this->mBottomLeftRadius = radius;
	this->mCornerOverrides |= BottomLeft;
	emit this->bottomLeftRadiusChanged();
}

void TahoeGlassRegion::resetBottomLeftRadius() {
	if (!(this->mCornerOverrides & BottomLeft)) return;
	this->mCornerOverrides &= ~BottomLeft;
	emit this->bottomLeftRadiusChanged();
}

qint32 TahoeGlassRegion::bottomRightRadius() const {
	return (this->mCornerOverrides & BottomRight) ? this->mBottomRightRadius : this->mRadius;
}

void TahoeGlassRegion::setBottomRightRadius(qint32 radius) {
	if ((this->mCornerOverrides & BottomRight) && radius == this->mBottomRightRadius) return;
	this->mBottomRightRadius = radius;
	this->mCornerOverrides |= BottomRight;
	emit this->bottomRightRadiusChanged();
}

void TahoeGlassRegion::resetBottomRightRadius() {
	if (!(this->mCornerOverrides & BottomRight)) return;
	this->mCornerOverrides &= ~BottomRight;
	emit this->bottomRightRadiusChanged();
}

bool TahoeGlassRegion::blur() const { return this->mBlur; }

void TahoeGlassRegion::setBlur(bool blur) {
	if (blur == this->mBlur) return;
	this->mBlur = blur;
	emit this->blurChanged();
}

bool TahoeGlassRegion::shadow() const { return this->mShadow; }

void TahoeGlassRegion::setShadow(bool shadow) {
	if (shadow == this->mShadow) return;
	this->mShadow = shadow;
	emit this->shadowChanged();
}

bool TahoeGlassRegion::clip() const { return this->mClip; }

void TahoeGlassRegion::setClip(bool clip) {
	if (clip == this->mClip) return;
	this->mClip = clip;
	emit this->clipChanged();
}

bool TahoeGlassRegion::enabled() const { return this->mEnabled; }

void TahoeGlassRegion::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;
	emit this->enabledChanged();
}

qreal TahoeGlassRegion::interaction() const { return this->mInteraction; }

void TahoeGlassRegion::setInteraction(qreal interaction) {
	auto clamped = qBound(0.0, interaction, 1.0);
	if (qFuzzyCompare(clamped, this->mInteraction)) return;
	this->mInteraction = clamped;
	emit this->interactionChanged();
}

qreal TahoeGlassRegion::materialAlpha() const { return this->mMaterialAlpha; }

void TahoeGlassRegion::setMaterialAlpha(qreal materialAlpha) {
	auto clamped = qBound(0.0, materialAlpha, 1.0);
	if (qFuzzyCompare(clamped, this->mMaterialAlpha)) return;
	this->mMaterialAlpha = clamped;
	emit this->materialAlphaChanged();
}

bool TahoeGlassRegion::buildLogicalRegion(impl::TahoeGlassRegionState* state) const {
	return this->buildRegion(state);
}

bool TahoeGlassRegion::buildSurfaceRegion(
    QWaylandWindow* waylandWindow,
    impl::TahoeGlassRegionState* state
) const {
	if (!waylandWindow || !this->buildRegion(state)) return false;

	// TahoeGlass regions are surface-local logical coordinates. The compositor
	// adds the surface's output location when sampling the background.
	auto margins = waylandWindow->clientSideMargins();
	state->rect.translate(margins.left(), margins.top());
	return true;
}

void TahoeGlassRegion::onItemDestroyed() {
	this->mItem = nullptr;
	emit this->itemChanged();
}

void TahoeGlassRegion::onItemGeometryChanged() { emit this->changed(); }

bool TahoeGlassRegion::buildRegion(impl::TahoeGlassRegionState* state) const {
	if (!state || !this->mEnabled) return false;

	QRectF rect;
	if (this->mItem != nullptr) {
		if (!this->mItem->isVisible()) return false;

		auto origin = this->mItem->mapToScene(QPointF(0, 0));
		auto extent = this->mItem->mapToScene(QPointF(this->mItem->width(), this->mItem->height()));
		rect = QRectF(origin, extent).normalized();
	} else {
		rect = QRectF(this->mX, this->mY, this->mWidth, this->mHeight).normalized();
	}

	if (rect.width() <= 0 || rect.height() <= 0) return false;

	auto x = static_cast<qint32>(std::floor(rect.x()));
	auto y = static_cast<qint32>(std::floor(rect.y()));
	auto width = static_cast<qint32>(std::ceil(rect.right()) - x);
	auto height = static_cast<qint32>(std::ceil(rect.bottom()) - y);
	if (width <= 0 || height <= 0) return false;

	state->id = this->mRegionId;
	state->rect = QRect(x, y, width, height);
	state->material = this->mMaterial.isEmpty() ? QStringLiteral("panel") : this->mMaterial;
	state->corners.topLeft = std::max(this->topLeftRadius(), 0);
	state->corners.topRight = std::max(this->topRightRadius(), 0);
	state->corners.bottomRight = std::max(this->bottomRightRadius(), 0);
	state->corners.bottomLeft = std::max(this->bottomLeftRadius(), 0);
	state->flags = (this->mBlur ? 1 : 0) | (this->mShadow ? 2 : 0) | (this->mClip ? 4 : 0);
	state->interaction = this->mInteraction;
	state->materialAlpha = this->mMaterialAlpha;
	return true;
}

TahoeGlass* TahoeGlass::qmlAttachedProperties(QObject* object) {
	auto* proxyWindow = qobject_cast<ProxyWindowBase*>(object);

	if (!proxyWindow) {
		if (auto* iface = qobject_cast<WindowInterface*>(object)) {
			proxyWindow = iface->proxyWindow();
		}
	}

	if (!proxyWindow) return nullptr;
	return new TahoeGlass(proxyWindow);
}

TahoeGlass::TahoeGlass(ProxyWindowBase* window): QObject(nullptr), proxyWindow(window) {
	QObject::connect(window, &ProxyWindowBase::windowConnected, this, &TahoeGlass::onWindowConnected);
	QObject::connect(window, &ProxyWindowBase::polished, this, &TahoeGlass::onWindowPolished);
	QObject::connect(window, &ProxyWindowBase::devicePixelRatioChanged, this, &TahoeGlass::updateRegions);
	QObject::connect(window, &QObject::destroyed, this, &TahoeGlass::onProxyWindowDestroyed);

	if (window->backingWindow()) {
		this->onWindowConnected();
	}
}

QQmlListProperty<TahoeGlassRegion> TahoeGlass::regions() {
	return QQmlListProperty<TahoeGlassRegion>(
	    this,
	    nullptr,
	    &TahoeGlass::regionsAppend,
	    &TahoeGlass::regionsCount,
	    &TahoeGlass::regionAt,
	    &TahoeGlass::regionsClear,
	    &TahoeGlass::regionsReplace,
	    &TahoeGlass::regionsRemoveLast
	);
}

bool TahoeGlass::available() const { return this->mAvailable; }

bool TahoeGlass::fallbackEnabled() const { return this->mFallbackEnabled; }

void TahoeGlass::setFallbackEnabled(bool enabled) {
	if (enabled == this->mFallbackEnabled) return;
	this->mFallbackEnabled = enabled;
	if (!enabled) this->clearFallback();
	this->updateRegions();
	emit this->fallbackEnabledChanged();
}

bool TahoeGlass::eventFilter(QObject* object, QEvent* event) {
	if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = dynamic_cast<QPlatformSurfaceEvent*>(event);
		if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
			this->surface = nullptr;
			this->pendingRegions = false;
			this->setAvailable(false);
		}
	} else if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
		// Catch window geometry changes that don't trigger x/y/width/height signals
		// This is crucial for niri compositor where window moves may not emit signals
		this->updateRegions();
	} else if (event->type() == QEvent::UpdateRequest) {
		// Also update on frame requests to ensure blur stays synchronized
		// with window position during animations/transitions
		if (this->pendingRegions) {
			this->updateRegions();
		}
	}

	return this->QObject::eventFilter(object, event);
}

void TahoeGlass::onWindowConnected() {
	this->mWindow = this->proxyWindow->backingWindow();
	this->mWindow->installEventFilter(this);

	QObject::connect(this->mWindow, &QWindow::visibleChanged, this, &TahoeGlass::onWindowVisibleChanged);
	QObject::connect(this->mWindow, &QWindow::xChanged, this, &TahoeGlass::updateRegions);
	QObject::connect(this->mWindow, &QWindow::yChanged, this, &TahoeGlass::updateRegions);
	QObject::connect(this->mWindow, &QWindow::widthChanged, this, &TahoeGlass::updateRegions);
	QObject::connect(this->mWindow, &QWindow::heightChanged, this, &TahoeGlass::updateRegions);

	this->onWindowVisibleChanged();
}

void TahoeGlass::onWindowVisibleChanged() {
	if (this->mWindow->isVisible()) {
		if (!this->mWindow->handle()) {
			this->mWindow->create();
		}
	}

	auto* window = dynamic_cast<QWaylandWindow*>(this->mWindow->handle());
	if (window == this->mWaylandWindow) return;

	if (this->mWaylandWindow) {
		QObject::disconnect(this->mWaylandWindow, nullptr, this, nullptr);
	}

	this->mWaylandWindow = window;
	if (!window) return;

	QObject::connect(this->mWaylandWindow, &QObject::destroyed, this, &TahoeGlass::onWaylandWindowDestroyed);
	QObject::connect(this->mWaylandWindow, &QWaylandWindow::surfaceCreated, this, &TahoeGlass::onWaylandSurfaceCreated);
	QObject::connect(this->mWaylandWindow, &QWaylandWindow::surfaceDestroyed, this, &TahoeGlass::onWaylandSurfaceDestroyed);

	if (this->mWaylandWindow->surface()) {
		this->onWaylandSurfaceCreated();
	}
}

void TahoeGlass::onWaylandWindowDestroyed() {
	this->mWaylandWindow = nullptr;
	this->setAvailable(false);
}

void TahoeGlass::onWaylandSurfaceCreated() {
	auto v = this->mWaylandWindow->property("qs_tahoe_glass");
	if (v.canConvert<TahoeGlass*>()) {
		auto* prev = v.value<TahoeGlass*>();
		if (prev != this && prev->surface) {
			this->surface.swap(prev->surface);
		}
	}

	if (!this->surface) {
		if (auto* manager = impl::TahoeGlassManager::instance()) {
			this->surface = std::unique_ptr<impl::TahoeGlassSurface>(
			    manager->createGlassSurface(this->mWaylandWindow)
			);
		}
	}

	this->mWaylandWindow->setProperty("qs_tahoe_glass", QVariant::fromValue(this));
	this->setAvailable(this->surface != nullptr);
	this->pendingRegions = true;
	if (this->proxyWindow) this->proxyWindow->schedulePolish();
}

void TahoeGlass::onWaylandSurfaceDestroyed() {
	this->surface = nullptr;
	this->pendingRegions = false;
	this->setAvailable(false);

	if (!this->proxyWindow) {
		this->deleteLater();
	}
}

void TahoeGlass::onProxyWindowDestroyed() {
	this->proxyWindow = nullptr;
	this->fallbackEffect = nullptr;

	if (this->surface == nullptr) {
		this->deleteLater();
	}
}

void TahoeGlass::onRegionDestroyed() {
	this->mRegions.removeAll(qobject_cast<TahoeGlassRegion*>(this->sender()));
	this->updateRegions();
	emit this->regionsChanged();
}

void TahoeGlass::updateRegions() {
	if (!this->proxyWindow) return;
	this->pendingRegions = true;
	this->proxyWindow->schedulePolish();
}

void TahoeGlass::onWindowPolished() {
	if (!this->pendingRegions || !this->mWaylandWindow || !this->mWaylandWindow->surface()) return;
	this->pendingRegions = false;

	if (!this->surface) {
		if (auto* manager = impl::TahoeGlassManager::instance()) {
			this->surface = std::unique_ptr<impl::TahoeGlassSurface>(
			    manager->createGlassSurface(this->mWaylandWindow)
			);
		}
	}

	QList<impl::TahoeGlassRegionState> logicalRegions;
	QList<impl::TahoeGlassRegionState> surfaceRegions;

	for (auto* region: this->mRegions) {
		if (!region) continue;

		impl::TahoeGlassRegionState logical;
		if (!region->buildLogicalRegion(&logical)) continue;
		logicalRegions.append(logical);

		impl::TahoeGlassRegionState surfaceRegion;
		if (region->buildSurfaceRegion(this->mWaylandWindow, &surfaceRegion)) {
			surfaceRegions.append(surfaceRegion);
		}

		if (surfaceRegions.size() >= 32) break;
	}

	if (this->surface) {
		this->surface->setRegions(surfaceRegions);
		this->setAvailable(true);
		this->clearFallback();
	} else {
		this->setAvailable(false);
		this->updateFallback(logicalRegions);
	}
}

void TahoeGlass::regionsAppend(QQmlListProperty<TahoeGlassRegion>* prop, TahoeGlassRegion* region) {
	auto* self = static_cast<TahoeGlass*>(prop->object); // NOLINT
	if (!region) return;

	QObject::connect(region, &QObject::destroyed, self, &TahoeGlass::onRegionDestroyed);
	QObject::connect(region, &TahoeGlassRegion::changed, self, &TahoeGlass::updateRegions);

	self->mRegions.append(region);
	self->updateRegions();
	emit self->regionsChanged();
}

TahoeGlassRegion* TahoeGlass::regionAt(QQmlListProperty<TahoeGlassRegion>* prop, qsizetype i) {
	return static_cast<TahoeGlass*>(prop->object)->mRegions.at(i); // NOLINT
}

void TahoeGlass::regionsClear(QQmlListProperty<TahoeGlassRegion>* prop) {
	auto* self = static_cast<TahoeGlass*>(prop->object); // NOLINT

	for (auto* region: self->mRegions) {
		QObject::disconnect(region, nullptr, self, nullptr);
	}

	self->mRegions.clear();
	self->updateRegions();
	emit self->regionsChanged();
}

qsizetype TahoeGlass::regionsCount(QQmlListProperty<TahoeGlassRegion>* prop) {
	return static_cast<TahoeGlass*>(prop->object)->mRegions.length(); // NOLINT
}

void TahoeGlass::regionsRemoveLast(QQmlListProperty<TahoeGlassRegion>* prop) {
	auto* self = static_cast<TahoeGlass*>(prop->object); // NOLINT

	auto* last = self->mRegions.last();
	if (last != nullptr) QObject::disconnect(last, nullptr, self, nullptr);

	self->mRegions.removeLast();
	self->updateRegions();
	emit self->regionsChanged();
}

void TahoeGlass::regionsReplace(
    QQmlListProperty<TahoeGlassRegion>* prop,
    qsizetype i,
    TahoeGlassRegion* region
) {
	auto* self = static_cast<TahoeGlass*>(prop->object); // NOLINT

	auto* old = self->mRegions.at(i);
	if (old != nullptr) QObject::disconnect(old, nullptr, self, nullptr);

	if (region != nullptr) {
		QObject::connect(region, &QObject::destroyed, self, &TahoeGlass::onRegionDestroyed);
		QObject::connect(region, &TahoeGlassRegion::changed, self, &TahoeGlass::updateRegions);
	}

	self->mRegions.replace(i, region);
	self->updateRegions();
	emit self->regionsChanged();
}

void TahoeGlass::setAvailable(bool available) {
	if (available == this->mAvailable) return;
	this->mAvailable = available;
	emit this->availableChanged();
}

void TahoeGlass::clearFallback() {
	if (this->fallbackEffect) {
		this->fallbackEffect->setBlurRegion(nullptr);
	}

	delete this->fallbackRegion;
	this->fallbackRegion = nullptr;
}

void TahoeGlass::updateFallback(const QList<impl::TahoeGlassRegionState>& regions) {
	if (!this->mFallbackEnabled || !this->proxyWindow) {
		this->clearFallback();
		return;
	}

	if (!this->fallbackEffect) {
		this->fallbackEffect = qobject_cast<background_effect::BackgroundEffect*>(
		    qmlAttachedPropertiesObject<background_effect::BackgroundEffect>(this->proxyWindow, true)
		);

		if (this->fallbackEffect) {
			QObject::connect(this->fallbackEffect, &QObject::destroyed, this, [this]() {
				this->fallbackEffect = nullptr;
				delete this->fallbackRegion;
				this->fallbackRegion = nullptr;
			});
		}
	}

	if (!this->fallbackEffect) return;

	auto* root = new PendingRegion(this);
	auto prop = root->regions();
	auto fallbackCount = 0;

	for (const auto& region: regions) {
		if ((region.flags & 1) == 0) continue;

		auto* child = new PendingRegion(root);
		child->setProperty("x", region.rect.x());
		child->setProperty("y", region.rect.y());
		child->setProperty("width", region.rect.width());
		child->setProperty("height", region.rect.height());
		child->setTopLeftRadius(region.corners.topLeft);
		child->setTopRightRadius(region.corners.topRight);
		child->setBottomRightRadius(region.corners.bottomRight);
		child->setBottomLeftRadius(region.corners.bottomLeft);
		prop.append(&prop, child);

		if (++fallbackCount >= 32) break;
	}

	auto* oldRegion = this->fallbackRegion;
	this->fallbackRegion = fallbackCount > 0 ? root : nullptr;
	this->fallbackEffect->setBlurRegion(this->fallbackRegion);

	if (this->fallbackRegion == nullptr) {
		delete root;
	}

	delete oldRegion;
}

} // namespace qs::wayland::tahoe_glass
