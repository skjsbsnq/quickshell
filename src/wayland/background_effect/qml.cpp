#include "qml.hpp"
#include <memory>

#include <private/qhighdpiscaling_p.h>
#include <private/qwaylandwindow_p.h>
#include <qcoreevent.h>
#include <qevent.h>
#include <qlogging.h>
#include <qnumeric.h>
#include <qobject.h>
#include <qregion.h>
#include <qtmetamacros.h>
#include <qvariant.h>
#include <qwindow.h>

#include "../../core/region.hpp"
#include "../../window/proxywindow.hpp"
#include "../../window/windowinterface.hpp"
#include "manager.hpp"
#include "surface.hpp"

using QtWaylandClient::QWaylandWindow;

namespace qs::wayland::background_effect {

BackgroundEffect* BackgroundEffect::qmlAttachedProperties(QObject* object) {
	auto* proxyWindow = ProxyWindowBase::forObject(object);
	if (!proxyWindow) return nullptr;
	return new BackgroundEffect(proxyWindow);
}

BackgroundEffect::BackgroundEffect(ProxyWindowBase* window): AttachedSurfaceLifecycle(window) {
	QObject::connect(window, &ProxyWindowBase::polished, this, &BackgroundEffect::onWindowPolished);

	QObject::connect(
	    window,
	    &ProxyWindowBase::devicePixelRatioChanged,
	    this,
	    &BackgroundEffect::updateBlurRegion
	);

	this->initializeLifecycle();
}

PendingRegion* BackgroundEffect::blurRegion() const { return this->mBlurRegion; }

void BackgroundEffect::setBlurRegion(PendingRegion* region) {
	if (region == this->mBlurRegion) return;

	if (this->mBlurRegion) {
		QObject::disconnect(this->mBlurRegion, nullptr, this, nullptr);
	}

	this->mBlurRegion = region;

	if (region) {
		QObject::connect(region, &QObject::destroyed, this, &BackgroundEffect::onBlurRegionDestroyed);
		QObject::connect(region, &PendingRegion::changed, this, &BackgroundEffect::updateBlurRegion);
	}

	this->updateBlurRegion();
	emit this->blurRegionChanged();
}

void BackgroundEffect::onBlurRegionDestroyed() {
	this->mBlurRegion = nullptr;
	this->updateBlurRegion();
	emit this->blurRegionChanged();
}

void BackgroundEffect::updateBlurRegion() {
	if (!this->surface || !this->proxyWindow) return;

	this->pendingBlurRegion = true;
	this->proxyWindow->schedulePolish();
}

void BackgroundEffect::onWindowPolished() {
	if (!this->surface || !this->pendingBlurRegion) return;
	if (!this->mWaylandWindow || !this->mWaylandWindow->surface()) {
		this->pendingBlurRegion = false;
		return;
	}

	QRegion region;
	if (this->mBlurRegion) {
		region =
		    this->mBlurRegion->applyTo(QRect(0, 0, this->mWindow->width(), this->mWindow->height()));

		auto scale = QHighDpiScaling::factor(this->mWindow);
		if (!qFuzzyCompare(scale, 1.0)) {
			region = QHighDpi::scale(region, scale);
		}

		auto margins = this->mWaylandWindow->clientSideMargins();
		region.translate(margins.left(), margins.top());
	}

	this->surface->setBlurRegion(region);
	this->pendingBlurRegion = false;
}

void BackgroundEffect::platformSurfaceAboutToBeDestroyed() {
	this->surface = nullptr;
	this->pendingBlurRegion = false;
}

void BackgroundEffect::waylandSurfaceCreated() {
	auto* manager = impl::BackgroundEffectManager::instance();

	if (!manager) {
		qWarning() << "Cannot enable background effect as ext-background-effect-v1 is not supported "
		              "by the current compositor.";
		return;
	}

	// Steal protocol surface from previous BackgroundEffect to avoid duplicate-attachment on reload.
	if (auto* prev = this->previousAttachedObject("qs_background_effect", this);
	    prev && prev->surface)
	{
		this->surface.swap(prev->surface);
		prev->pendingBlurRegion = false;

		if (!prev->proxyWindow) {
			prev->deleteLater();
		}
	}

	if (!this->surface) {
		this->surface = std::unique_ptr<impl::BackgroundEffectSurface>(
		    manager->createEffectSurface(this->mWaylandWindow)
		);
	}

	this->setAttachedObject("qs_background_effect", this);

	this->pendingBlurRegion = this->mBlurRegion != nullptr;
	if (this->pendingBlurRegion) {
		this->schedulePolish();
	}
}

void BackgroundEffect::waylandSurfaceDestroyed() {
	this->surface = nullptr;
	this->pendingBlurRegion = false;

	if (!this->proxyWindow) {
		this->deleteLater();
	}
}

void BackgroundEffect::proxyWindowDestroyed() {
	// Don't delete the BackgroundEffect, and therefore the impl::BackgroundEffectSurface
	// until the wl_surface is destroyed. Deleting it when the proxy window is deleted would
	// cause a frame without blur between the destruction of the ext_background_effect_surface_v1
	// and wl_surface objects.

	if (this->surface == nullptr) {
		this->deleteLater();
	}
}

} // namespace qs::wayland::background_effect
