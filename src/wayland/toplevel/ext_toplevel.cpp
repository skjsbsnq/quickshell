#include "ext_toplevel.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qwayland-ext-foreign-toplevel-list-v1.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::toplevel::ext {

QS_LOGGING_CATEGORY(logExtToplevelList, "quickshell.wayland.extToplevelList", QtWarningMsg);

ExtToplevelHandle::ExtToplevelHandle(::ext_foreign_toplevel_handle_v1* handle)
    : QtWayland::ext_foreign_toplevel_handle_v1(handle) {}

QString ExtToplevelHandle::identifier() const { return this->mIdentifier; }
QString ExtToplevelHandle::appId() const { return this->mAppId; }
QString ExtToplevelHandle::title() const { return this->mTitle; }
bool ExtToplevelHandle::ready() const { return this->isReady; }

void ExtToplevelHandle::ext_foreign_toplevel_handle_v1_closed() {
	qCDebug(logExtToplevelList) << this << "closed";
	this->destroy();
	emit this->closed();
	delete this;
}

void ExtToplevelHandle::ext_foreign_toplevel_handle_v1_done() {
	if (this->isReady) return;
	if (this->mIdentifier.isEmpty()) {
		qCWarning(logExtToplevelList) << this << "done without identifier; leave unpaired";
		return;
	}
	this->isReady = true;
	qCDebug(logExtToplevelList) << this << "ready identifier=" << this->mIdentifier;
	emit this->readyChanged();
}

void ExtToplevelHandle::ext_foreign_toplevel_handle_v1_title(const QString& title) {
	this->mTitle = title;
}

void ExtToplevelHandle::ext_foreign_toplevel_handle_v1_app_id(const QString& appId) {
	this->mAppId = appId;
}

void ExtToplevelHandle::ext_foreign_toplevel_handle_v1_identifier(const QString& identifier) {
	this->mIdentifier = identifier;
}

ExtToplevelList::ExtToplevelList(): QWaylandClientExtensionTemplate(1) { this->initialize(); }

bool ExtToplevelList::available() const { return this->isActive(); }

const QVector<ExtToplevelHandle*>& ExtToplevelList::readyHandles() const {
	return this->mReadyHandles;
}

ExtToplevelList* ExtToplevelList::instance() {
	static auto* instance = new ExtToplevelList(); // NOLINT
	return instance;
}

void ExtToplevelList::ext_foreign_toplevel_list_v1_toplevel(
    ::ext_foreign_toplevel_handle_v1* toplevel
) {
	auto* handle = new ExtToplevelHandle(toplevel);
	QObject::connect(handle, &ExtToplevelHandle::closed, this, &ExtToplevelList::onHandleClosed);
	QObject::connect(
	    handle,
	    &ExtToplevelHandle::readyChanged,
	    this,
	    &ExtToplevelList::onHandleReady
	);
	qCDebug(logExtToplevelList) << "ext handle created" << handle;
	this->mHandles.push_back(handle);
}

void ExtToplevelList::ext_foreign_toplevel_list_v1_finished() {
	qCDebug(logExtToplevelList) << "ext list finished";
}

void ExtToplevelList::onHandleReady() {
	auto* handle = qobject_cast<ExtToplevelHandle*>(this->sender());
	if (handle == nullptr) return;
	this->mReadyHandles.push_back(handle);
	emit this->handleReady(handle);
}

void ExtToplevelList::onHandleClosed() {
	auto* handle = qobject_cast<ExtToplevelHandle*>(this->sender());
	if (handle == nullptr) return;
	this->mReadyHandles.removeOne(handle);
	this->mHandles.removeOne(handle);
}

} // namespace qs::wayland::toplevel::ext
