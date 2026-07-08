#pragma once

#include <qdbuscontext.h>
#include <qdbusextratypes.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::bluetooth {

class BluetoothAgent
    : public QObject
    , protected QDBusContext {
	Q_OBJECT;

public:
	explicit BluetoothAgent(QObject* parent = nullptr);
	~BluetoothAgent() override;

	void registerAgent();
	bool prepareDevice(const QString& path);
	void allowDevice(const QString& path);

	// NOLINTBEGIN
	void Release();
	QString RequestPinCode(const QDBusObjectPath& device);
	void DisplayPinCode(const QDBusObjectPath& device, const QString& pincode);
	quint32 RequestPasskey(const QDBusObjectPath& device);
	void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
	void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey);
	void RequestAuthorization(const QDBusObjectPath& device);
	void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
	void Cancel();
	// NOLINTEND

private:
	[[nodiscard]] bool isAllowed(const QDBusObjectPath& device) const;
	void reject(const QDBusObjectPath& device, const QString& reason);
	void unregisterAgent();

	static constexpr auto AgentPath = "/Quickshell/BluetoothAgent";
	QSet<QString> allowedDevices;
	bool objectRegistered = false;
	bool agentRegistered = false;
};

} // namespace qs::bluetooth
