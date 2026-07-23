#include "identifier_pairing.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"

namespace qs::wayland::toplevel {

QS_LOGGING_CATEGORY(logIdentifierPairing, "quickshell.wayland.identifierPairing", QtWarningMsg);

IdentifierPairing::IdentifierPairing() {
	auto* extList = ext::ExtToplevelList::instance();
	auto* wlrManager = wlr::ToplevelManager::instance();

	// Always watch for late ext-list activation (registry race / construct order).
	QObject::connect(
	    extList,
	    &QWaylandClientExtension::activeChanged,
	    this,
	    &IdentifierPairing::onExtListActiveChanged
	);
	this->connectExtList();

	QObject::connect(
	    wlrManager,
	    &wlr::ToplevelManager::toplevelReady,
	    this,
	    &IdentifierPairing::onWlrReady
	);
	for (auto* handle: wlrManager->readyToplevels()) {
		this->onWlrReady(handle);
	}
}

IdentifierPairing* IdentifierPairing::instance() {
	static auto* instance = new IdentifierPairing(); // NOLINT
	return instance;
}

void IdentifierPairing::connectExtList() {
	auto* extList = ext::ExtToplevelList::instance();
	if (!extList->available()) {
		qCWarning(logIdentifierPairing)
		    << "ext-foreign-toplevel-list-v1 not active yet; will retry on activeChanged";
		return;
	}
	if (this->mExtConnected) return;

	QObject::connect(
	    extList,
	    &ext::ExtToplevelList::handleReady,
	    this,
	    &IdentifierPairing::onExtReady
	);
	this->mExtConnected = true;
	for (auto* handle: extList->readyHandles()) {
		this->onExtReady(handle);
	}
	qCDebug(logIdentifierPairing) << "ext list connected for identifier pairing";
}

void IdentifierPairing::onExtListActiveChanged() {
	auto* extList = ext::ExtToplevelList::instance();
	if (!extList->available()) {
		// List went inactive (stop/disconnect). Drop unpaired ext; do not invent fuzzy ids.
		this->mUnpairedExt.clear();
		this->mExtConnected = false;
		this->failClosed(QStringLiteral("ext list became inactive"));
		return;
	}
	this->connectExtList();
	emit this->statsChanged();
}

void IdentifierPairing::onExtReady(ext::ExtToplevelHandle* handle) {
	if (handle == nullptr) return;
	if (this->mUnpairedExt.contains(handle)) return;
	QObject::connect(
	    handle,
	    &ext::ExtToplevelHandle::closed,
	    this,
	    &IdentifierPairing::onExtClosed,
	    Qt::UniqueConnection
	);
	this->mUnpairedExt.push_back(handle);
	this->tryPair();
	emit this->statsChanged();
}

void IdentifierPairing::onWlrReady(wlr::ToplevelHandle* handle) {
	if (handle == nullptr) return;
	if (this->mUnpairedWlr.contains(handle)) return;
	QObject::connect(
	    handle,
	    &wlr::ToplevelHandle::closed,
	    this,
	    &IdentifierPairing::onWlrClosed,
	    Qt::UniqueConnection
	);
	this->mUnpairedWlr.push_back(handle);
	this->tryPair();
	emit this->statsChanged();
}

void IdentifierPairing::onExtClosed() {
	auto* handle = qobject_cast<ext::ExtToplevelHandle*>(this->sender());
	if (handle == nullptr) return;
	this->mUnpairedExt.removeOne(handle);

	// List stop / ext-only close: clear matching wlr identifiers fail-closed so
	// Shell cannot act on a stale id while the action handle still lives.
	const auto id = handle->identifier().trimmed();
	if (!id.isEmpty()) {
		for (auto* wlr: wlr::ToplevelManager::instance()->readyToplevels()) {
			if (wlr->identifier() == id) {
				wlr->setIdentifier(QString());
				qCWarning(logIdentifierPairing)
				    << "cleared wlr identifier after ext close:" << id;
			}
		}
	}
	emit this->statsChanged();
}

void IdentifierPairing::onWlrClosed() {
	auto* handle = qobject_cast<wlr::ToplevelHandle*>(this->sender());
	if (handle == nullptr) return;
	this->mUnpairedWlr.removeOne(handle);
	// Handle is about to delete; identifier dies with it.
	emit this->statsChanged();
}

void IdentifierPairing::tryPair() {
	while (!this->mUnpairedExt.isEmpty() && !this->mUnpairedWlr.isEmpty()) {
		auto* extHandle = this->mUnpairedExt.takeFirst();
		auto* wlrHandle = this->mUnpairedWlr.takeFirst();

		const auto extApp = extHandle->appId().trimmed();
		const auto wlrApp = wlrHandle->appId().trimmed();
		if (!extApp.isEmpty() && !wlrApp.isEmpty() && extApp != wlrApp) {
			this->failClosed(
			    QStringLiteral("appId desync on FIFO pair ext=%1 wlr=%2 identifier=%3")
			        .arg(extApp, wlrApp, extHandle->identifier())
			);
			// Consume both; do not assign identifier.
			continue;
		}

		const auto identifier = extHandle->identifier().trimmed();
		if (identifier.isEmpty()) {
			this->failClosed(QStringLiteral("empty identifier on ready ext handle"));
			continue;
		}

		wlrHandle->setIdentifier(identifier);
		this->mPairCount += 1;
		qCDebug(logIdentifierPairing) << "paired identifier" << identifier << "to" << wlrHandle;
	}
	emit this->statsChanged();
}

void IdentifierPairing::failClosed(const QString& reason) {
	this->mDesyncCount += 1;
	qCWarning(logIdentifierPairing) << "pairing fail-closed:" << reason
	                                << "desyncCount=" << this->mDesyncCount;
	emit this->statsChanged();
}

} // namespace qs::wayland::toplevel
