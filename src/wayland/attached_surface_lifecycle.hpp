#pragma once

#include <qobject.h>
#include <qvariant.h>

class ProxyWindowBase;
class QEvent;
class QWindow;

namespace QtWaylandClient {
class QWaylandWindow;
}

namespace qs::wayland {

class AttachedSurfaceLifecycle: public QObject {
public:
	explicit AttachedSurfaceLifecycle(ProxyWindowBase* window);

	void initializeLifecycle();

	[[nodiscard]] QWindow* backingWindow() const;
	[[nodiscard]] QtWaylandClient::QWaylandWindow* waylandWindow() const;

	bool eventFilter(QObject* object, QEvent* event) override;

protected:
	void schedulePolish() const;

	[[nodiscard]] QVariant attachedObjectProperty(const char* propertyName) const;
	void setAttachedObjectProperty(const char* propertyName, const QVariant& value);

	template <typename Owner>
	[[nodiscard]] Owner* previousAttachedObject(const char* propertyName, Owner* current) const {
		auto value = this->attachedObjectProperty(propertyName);
		if (!value.canConvert<Owner*>()) return nullptr;

		auto* previous = value.value<Owner*>();
		return previous == current ? nullptr : previous;
	}

	template <typename Owner>
	void setAttachedObject(const char* propertyName, Owner* owner) {
		this->setAttachedObjectProperty(propertyName, QVariant::fromValue(owner));
	}

	virtual void backingWindowConnected() {}
	virtual void platformSurfaceAboutToBeDestroyed() {}
	virtual void waylandWindowDestroyed() {}
	virtual void waylandSurfaceCreated() {}
	virtual void waylandSurfaceDestroyed() {}
	virtual void proxyWindowDestroyed() {}
	virtual bool filteredWindowEvent(QObject* object, QEvent* event);

	ProxyWindowBase* proxyWindow = nullptr;
	QWindow* mWindow = nullptr;
	QtWaylandClient::QWaylandWindow* mWaylandWindow = nullptr;

private:
	void onWindowConnected();
	void onWindowVisibleChanged();
	void onBackingWindowDestroyed();
	void onWaylandWindowDestroyed();
	void onWaylandSurfaceCreated();
	void onWaylandSurfaceDestroyed();
	void onProxyWindowDestroyed();
	void detachBackingWindow();
	void detachWaylandWindow();

	bool mHasWaylandSurface = false;
};

} // namespace qs::wayland
