#pragma once

#include <qobject.h>
#include <qtmetamacros.h>
#include <qvector.h>

#include "../../core/logcat.hpp"
#include "ext_toplevel.hpp"
#include "wlr_toplevel.hpp"

namespace qs::wayland::toplevel {

QS_DECLARE_LOGGING_CATEGORY(logIdentifierPairing);

/// Coordinates ext-foreign-toplevel-list and wlr-foreign-toplevel-management
/// handle streams for niri (and compositors with the same dual-manager
/// creation order). Pairs FIFO by creation sequence and assigns the ext
/// decimal identifier onto the existing wlr handle.
///
/// Fail closed on appId mismatch at pair time or when streams desync:
/// leaves identifier empty and emits diagnostics. Never falls back to
/// appId/title fuzzy matching.
class IdentifierPairing: public QObject {
	Q_OBJECT;

public:
	static IdentifierPairing* instance();

	/// Diagnostic counters (observable from tests / logs).
	[[nodiscard]] int pairCount() const { return this->mPairCount; }
	[[nodiscard]] int desyncCount() const { return this->mDesyncCount; }
	[[nodiscard]] int unpairedExtCount() const { return this->mUnpairedExt.size(); }
	[[nodiscard]] int unpairedWlrCount() const { return this->mUnpairedWlr.size(); }

signals:
	void statsChanged();

private slots:
	void onExtReady(ext::ExtToplevelHandle* handle);
	void onWlrReady(wlr::ToplevelHandle* handle);
	void onExtClosed();
	void onWlrClosed();
	void onExtListActiveChanged();

private:
	explicit IdentifierPairing();

	void connectExtList();
	void tryPair();
	void failClosed(const QString& reason);

	bool mExtConnected = false;

	QVector<ext::ExtToplevelHandle*> mUnpairedExt;
	QVector<wlr::ToplevelHandle*> mUnpairedWlr;
	int mPairCount = 0;
	int mDesyncCount = 0;
};

} // namespace qs::wayland::toplevel
