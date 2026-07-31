#include "agent.hpp"

#include <qdbusconnection.h>
#include <qdbuspendingcall.h>
#include <qdbuspendingreply.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qtimer.h>

#include "../core/logcat.hpp"
#include "dbus_agent.h"
#include "dbus_agentmanager.h"

namespace qs::bluetooth {

namespace {
QS_LOGGING_CATEGORY(logAgent, "quickshell.bluetooth.agent", QtWarningMsg);
}

BluetoothAgent::BluetoothAgent(QObject* parent): QObject(parent) {
	new DBusBluezAgentAdaptor(this);
}

BluetoothAgent::~BluetoothAgent() { this->unregisterAgent(); }

void BluetoothAgent::registerAgent() {
	if (this->agentRegistered) return;
	this->prepareDevice(QString());
}

bool BluetoothAgent::prepareDevice(const QString& path) {
	if (!this->agentRegistered && this->mRegisterWatcher == nullptr) {
		auto bus = QDBusConnection::systemBus();
		if (!bus.isConnected()) {
			qCWarning(logAgent) << "Could not connect to DBus. Bluetooth pairing agent is unavailable.";
			return false;
		}

		if (!this->objectRegistered) {
			if (!bus.registerObject(AgentPath, this)) {
				qCWarning(logAgent) << "Could not register Bluetooth pairing agent object with DBus.";
				return false;
			}

			this->objectRegistered = true;
		}

		DBusBluezAgentManagerInterface
		    manager("org.bluez", "/org/bluez", QDBusConnection::systemBus(), this);

		if (!manager.isValid()) {
			qCWarning(logAgent) << "Could not create BlueZ AgentManager interface.";
			return false;
		}

		// Register asynchronously so an unresponsive bluetoothd cannot freeze
		// the GUI thread (S-L3): the old reply.waitForFinished() blocked up to
		// the D-Bus timeout (25s). The result is handled when the call
		// completes; a reentrant prepareDevice() while a registration is in
		// flight is a no-op (mRegisterWatcher guards it).
		auto reply = manager.RegisterAgent(QDBusObjectPath(AgentPath), "NoInputNoOutput");
		this->mRegisterWatcher = new QDBusPendingCallWatcher(reply, this);

		QObject::connect(
		    this->mRegisterWatcher,
		    &QDBusPendingCallWatcher::finished,
		    this,
		    [this](QDBusPendingCallWatcher* watcher) {
			    const QDBusPendingReply<> reply = *watcher;

			    if (reply.isError()) {
				    qCWarning(logAgent).nospace()
				        << "Failed to register Bluetooth pairing agent: " << reply.error().message();
			    } else {
				    this->agentRegistered = true;
				    qCDebug(logAgent) << "Registered Bluetooth pairing agent";
			    }

			    this->mRegisterWatcher = nullptr;
			    watcher->deleteLater();
		    }
		);
	}

	this->allowDevice(path);
	return true;
}

void BluetoothAgent::allowDevice(const QString& path) {
	if (path.isEmpty()) return;

	this->allowedDevices.insert(path);
	QTimer::singleShot(120000, this, [this, path]() { this->allowedDevices.remove(path); });
}

void BluetoothAgent::Release() {
	qCDebug(logAgent) << "BlueZ released Bluetooth pairing agent";
	this->agentRegistered = false;
	this->allowedDevices.clear();
}

QString BluetoothAgent::RequestPinCode(const QDBusObjectPath& device) {
	this->reject(device, "PIN code pairing requires an interactive prompt.");
	return {};
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pincode) {
	if (!this->isAllowed(device)) {
		this->reject(device, "Device was not started from this pairing flow.");
		return;
	}

	qCInfo(logAgent).nospace() << "Bluetooth pairing PIN for " << device.path() << ": " << pincode;
}

quint32 BluetoothAgent::RequestPasskey(const QDBusObjectPath& device) {
	this->reject(device, "Passkey entry requires an interactive prompt.");
	return 0;
}

void BluetoothAgent::DisplayPasskey(
    const QDBusObjectPath& device,
    quint32 passkey,
    quint16 entered
) {
	if (!this->isAllowed(device)) {
		this->reject(device, "Device was not started from this pairing flow.");
		return;
	}

	qCInfo(logAgent).nospace() << "Bluetooth pairing passkey for " << device.path() << ": "
	                           << QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'))
	                           << " entered=" << entered;
}

void BluetoothAgent::RequestConfirmation(const QDBusObjectPath& device, quint32 passkey) {
	if (!this->isAllowed(device)) {
		this->reject(device, "Device was not started from this pairing flow.");
		return;
	}

	qCDebug(logAgent).nospace() << "Confirming Bluetooth pairing for " << device.path()
	                            << " with passkey "
	                            << QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'));
}

void BluetoothAgent::RequestAuthorization(const QDBusObjectPath& device) {
	if (!this->isAllowed(device)) {
		this->reject(device, "Device was not started from this pairing flow.");
		return;
	}

	qCDebug(logAgent) << "Authorizing Bluetooth pairing for" << device.path();
}

void BluetoothAgent::AuthorizeService(const QDBusObjectPath& device, const QString& uuid) {
	if (!this->isAllowed(device)) {
		this->reject(device, "Device was not started from this pairing flow.");
		return;
	}

	qCDebug(logAgent).nospace() << "Authorizing Bluetooth service " << uuid << " for "
	                            << device.path();
}

void BluetoothAgent::Cancel() { qCDebug(logAgent) << "Bluetooth pairing agent request canceled"; }

bool BluetoothAgent::isAllowed(const QDBusObjectPath& device) const {
	return this->allowedDevices.contains(device.path());
}

void BluetoothAgent::reject(const QDBusObjectPath& device, const QString& reason) {
	qCWarning(logAgent).nospace() << "Rejecting Bluetooth pairing request for " << device.path()
	                              << ": " << reason;
	this->sendErrorReply("org.bluez.Error.Rejected", reason);
}

void BluetoothAgent::unregisterAgent() {
	if (!this->agentRegistered) return;

	DBusBluezAgentManagerInterface
	    manager("org.bluez", "/org/bluez", QDBusConnection::systemBus(), this);

	if (manager.isValid()) {
		// Fire and forget: the old reply.waitForFinished() blocked the GUI
		// thread up to the D-Bus timeout when bluetoothd was unresponsive
		// (S-L3). The UnregisterAgent message is dispatched and the reply is
		// discarded; this runs from ~BluetoothAgent, where awaiting a reply
		// (blocking or via a watcher) is not an option anyway.
		manager.UnregisterAgent(QDBusObjectPath(AgentPath));
	}

	this->agentRegistered = false;
}

} // namespace qs::bluetooth
