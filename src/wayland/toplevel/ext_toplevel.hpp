#pragma once

#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvector.h>
#include <qwayland-ext-foreign-toplevel-list-v1.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"
#include "wayland-ext-foreign-toplevel-list-v1-client-protocol.h"

namespace qs::wayland::toplevel::ext {

QS_DECLARE_LOGGING_CATEGORY(logExtToplevelList);

/// One ext_foreign_toplevel_handle_v1 from ext-foreign-toplevel-list-v1.
/// Used only as the identity half of coordinated pairing with wlr handles.
class ExtToplevelHandle
    : public QObject
    , public QtWayland::ext_foreign_toplevel_handle_v1 {
	Q_OBJECT;

public:
	explicit ExtToplevelHandle(::ext_foreign_toplevel_handle_v1* handle);

	[[nodiscard]] QString identifier() const;
	[[nodiscard]] QString appId() const;
	[[nodiscard]] QString title() const;
	[[nodiscard]] bool ready() const;

signals:
	/// Emitted once after the first `done` when identifier is non-empty.
	void readyChanged();
	/// Emitted right before delete this.
	void closed();

private:
	void ext_foreign_toplevel_handle_v1_closed() override;
	void ext_foreign_toplevel_handle_v1_done() override;
	void ext_foreign_toplevel_handle_v1_title(const QString& title) override;
	void ext_foreign_toplevel_handle_v1_app_id(const QString& appId) override;
	void ext_foreign_toplevel_handle_v1_identifier(const QString& identifier) override;

	bool isReady = false;
	QString mIdentifier;
	QString mAppId;
	QString mTitle;
};

class ExtToplevelList
    : public QWaylandClientExtensionTemplate<ExtToplevelList>
    , public QtWayland::ext_foreign_toplevel_list_v1 {
	Q_OBJECT;

public:
	[[nodiscard]] bool available() const;
	[[nodiscard]] const QVector<ExtToplevelHandle*>& readyHandles() const;

	static ExtToplevelList* instance();

signals:
	void handleReady(ExtToplevelHandle* handle);

protected:
	explicit ExtToplevelList();

	void ext_foreign_toplevel_list_v1_toplevel(::ext_foreign_toplevel_handle_v1* toplevel) override;
	void ext_foreign_toplevel_list_v1_finished() override;

private slots:
	void onHandleReady();
	void onHandleClosed();

private:
	QVector<ExtToplevelHandle*> mHandles;
	QVector<ExtToplevelHandle*> mReadyHandles;
};

} // namespace qs::wayland::toplevel::ext
