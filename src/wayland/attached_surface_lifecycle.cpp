#include "attached_surface_lifecycle.hpp"

#include <private/qwaylandwindow_p.h>
#include <qcoreevent.h>
#include <qevent.h>
#include <qobject.h>
#include <qvariant.h>
#include <qwindow.h>

#include "../window/proxywindow.hpp"

using QtWaylandClient::QWaylandWindow;

namespace qs::wayland {

AttachedSurfaceLifecycle::AttachedSurfaceLifecycle(ProxyWindowBase* window)
    : QObject(nullptr)
    , proxyWindow(window) {
	if (!window) return;

	QObject::connect(
	    window,
	    &ProxyWindowBase::windowConnected,
	    this,
	    &AttachedSurfaceLifecycle::onWindowConnected
	);

	QObject::connect(
	    window,
	    &QObject::destroyed,
	    this,
	    &AttachedSurfaceLifecycle::onProxyWindowDestroyed
	);
}

void AttachedSurfaceLifecycle::initializeLifecycle() {
	if (this->proxyWindow && this->proxyWindow->backingWindow()) {
		this->onWindowConnected();
	}
}

QWindow* AttachedSurfaceLifecycle::backingWindow() const { return this->mWindow; }

QWaylandWindow* AttachedSurfaceLifecycle::waylandWindow() const { return this->mWaylandWindow; }

void AttachedSurfaceLifecycle::schedulePolish() const {
	if (this->proxyWindow) {
		this->proxyWindow->schedulePolish();
	}
}

QVariant AttachedSurfaceLifecycle::attachedObjectProperty(const char* propertyName) const {
	if (!this->mWaylandWindow) return {};
	return this->mWaylandWindow->property(propertyName);
}

void AttachedSurfaceLifecycle::setAttachedObjectProperty(
    const char* propertyName,
    const QVariant& value
) {
	if (this->mWaylandWindow) {
		this->mWaylandWindow->setProperty(propertyName, value);
	}
}

bool AttachedSurfaceLifecycle::eventFilter(QObject* object, QEvent* event) {
	if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = dynamic_cast<QPlatformSurfaceEvent*>(event);
		if (surfaceEvent
		    && surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
		{
			this->platformSurfaceAboutToBeDestroyed();
		}
	}

	if (this->filteredWindowEvent(object, event)) return true;
	return this->QObject::eventFilter(object, event);
}

bool AttachedSurfaceLifecycle::filteredWindowEvent(QObject* /*object*/, QEvent* /*event*/) {
	return false;
}

void AttachedSurfaceLifecycle::onWindowConnected() {
	if (!this->proxyWindow) return;

	auto* window = this->proxyWindow->backingWindow();
	if (!window) return;

	if (window != this->mWindow) {
		this->detachBackingWindow();

		this->mWindow = window;
		this->mWindow->installEventFilter(this);

		QObject::connect(
		    this->mWindow,
		    &QObject::destroyed,
		    this,
		    &AttachedSurfaceLifecycle::onBackingWindowDestroyed
		);

		QObject::connect(
		    this->mWindow,
		    &QWindow::visibleChanged,
		    this,
		    &AttachedSurfaceLifecycle::onWindowVisibleChanged
		);

		this->backingWindowConnected();
	}

	this->onWindowVisibleChanged();
}

void AttachedSurfaceLifecycle::onWindowVisibleChanged() {
	if (!this->mWindow) return;

	if (this->mWindow->isVisible()) {
		if (!this->mWindow->handle()) {
			this->mWindow->create();
		}
	}

	auto* window = dynamic_cast<QWaylandWindow*>(this->mWindow->handle());
	if (window == this->mWaylandWindow) return;

	this->detachWaylandWindow();
	this->mWaylandWindow = window;
	if (!window) return;

	QObject::connect(
	    this->mWaylandWindow,
	    &QObject::destroyed,
	    this,
	    &AttachedSurfaceLifecycle::onWaylandWindowDestroyed
	);

	QObject::connect(
	    this->mWaylandWindow,
	    &QWaylandWindow::surfaceCreated,
	    this,
	    &AttachedSurfaceLifecycle::onWaylandSurfaceCreated
	);

	QObject::connect(
	    this->mWaylandWindow,
	    &QWaylandWindow::surfaceDestroyed,
	    this,
	    &AttachedSurfaceLifecycle::onWaylandSurfaceDestroyed
	);

	if (this->mWaylandWindow->surface()) {
		this->onWaylandSurfaceCreated();
	}
}

void AttachedSurfaceLifecycle::onBackingWindowDestroyed() {
	this->detachWaylandWindow();
	this->mWindow = nullptr;
}

void AttachedSurfaceLifecycle::onWaylandWindowDestroyed() {
	if (this->mHasWaylandSurface) {
		this->mHasWaylandSurface = false;
		this->waylandSurfaceDestroyed();
	}

	this->mWaylandWindow = nullptr;
	this->waylandWindowDestroyed();
}

void AttachedSurfaceLifecycle::onWaylandSurfaceCreated() {
	if (this->mHasWaylandSurface) return;
	this->mHasWaylandSurface = true;
	this->waylandSurfaceCreated();
}

void AttachedSurfaceLifecycle::onWaylandSurfaceDestroyed() {
	if (!this->mHasWaylandSurface) return;
	this->mHasWaylandSurface = false;
	this->waylandSurfaceDestroyed();
}

void AttachedSurfaceLifecycle::onProxyWindowDestroyed() {
	this->proxyWindow = nullptr;
	this->proxyWindowDestroyed();
}

void AttachedSurfaceLifecycle::detachBackingWindow() {
	if (!this->mWindow) return;

	this->detachWaylandWindow();
	this->mWindow->removeEventFilter(this);
	QObject::disconnect(this->mWindow, nullptr, this, nullptr);
	this->mWindow = nullptr;
}

void AttachedSurfaceLifecycle::detachWaylandWindow() {
	if (!this->mWaylandWindow) return;

	if (this->mHasWaylandSurface) {
		this->mHasWaylandSurface = false;
		this->waylandSurfaceDestroyed();
	}

	QObject::disconnect(this->mWaylandWindow, nullptr, this, nullptr);
	this->mWaylandWindow = nullptr;
	this->waylandWindowDestroyed();
}

} // namespace qs::wayland
