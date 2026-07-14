#pragma once

#include <qdatetime.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#ifdef QS_TEST
#include <functional>
#endif

///! System clock accessor.
/// SystemClock is a view into the system's clock.
/// It updates at hour, minute, or second intervals depending on @@precision.
///
/// # Examples
/// ```qml
/// SystemClock {
///   id: clock
///   precision: SystemClock.Seconds
/// }
///
/// @@QtQuick.Text {
///   text: Qt.formatDateTime(clock.date, "hh:mm:ss - yyyy-MM-dd")
/// }
/// ```
///
/// > [!WARNING] Clock updates will trigger within 50ms of the system clock changing,
/// > however this can be either before or after the clock changes (+-50ms). If you
/// > need a date object, use @@date instead of constructing a new one, or the time
/// > of the constructed object could be off by up to a second.
///
/// # Wall-clock jump convergence
/// SystemClock has no OS time-change notification. Guarantees:
/// - @@resync() samples the wall clock immediately and updates @@date.
/// - Without an explicit resync, forward/backward jumps and timezone/offset
///   changes converge at the next precision-boundary timer fire (Minutes:
///   next minute boundary; not an active sub-minute notification).
/// - When a timer fires more than 500ms off its target, the real wall clock
///   is used instead of the predicted target.
class SystemClock: public QObject {
	Q_OBJECT;
	/// If the clock should update. Defaults to true.
	///
	/// Setting enabled to false pauses the clock.
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	/// The precision the clock should measure at. Defaults to `SystemClock.Seconds`.
	Q_PROPERTY(SystemClock::Enum precision READ precision WRITE setPrecision NOTIFY precisionChanged);
	/// The current date and time.
	///
	/// > [!TIP] You can use @@QtQml.Qt.formatDateTime() to get the time as a string in
	/// > your format of choice.
	Q_PROPERTY(QDateTime date READ date NOTIFY dateChanged);
	/// The current hour.
	Q_PROPERTY(quint32 hours READ hours NOTIFY dateChanged);
	/// The current minute, or 0 if @@precision is `SystemClock.Hours`.
	Q_PROPERTY(quint32 minutes READ minutes NOTIFY dateChanged);
	/// The current second, or 0 if @@precision is `SystemClock.Hours` or `SystemClock.Minutes`.
	Q_PROPERTY(quint32 seconds READ seconds NOTIFY dateChanged);
	QML_ELEMENT;

public:
	// must be named enum until docgen is ready to handle member enums better
	enum Enum : quint8 {
		Hours = 1,
		Minutes = 2,
		Seconds = 3,
	};
	Q_ENUM(Enum);

	explicit SystemClock(QObject* parent = nullptr);

	[[nodiscard]] bool enabled() const;
	void setEnabled(bool enabled);

	[[nodiscard]] SystemClock::Enum precision() const;
	void setPrecision(SystemClock::Enum precision);

	[[nodiscard]] QDateTime date() const { return this->currentTime; }
	[[nodiscard]] quint32 hours() const { return this->currentTime.time().hour(); }
	[[nodiscard]] quint32 minutes() const { return this->currentTime.time().minute(); }
	[[nodiscard]] quint32 seconds() const { return this->currentTime.time().second(); }

	/// Immediately sample the wall clock into @@date.
	/// When enabled, also reschedules the next precision-boundary timer.
	/// When disabled, updates @@date but leaves the timer stopped.
	/// This is the sole authorized explicit refresh entry (no updateNow/refresh aliases).
	Q_INVOKABLE void resync();

signals:
	void enabledChanged();
	void precisionChanged();
	void dateChanged();

private slots:
	void onTimeout();

private:
	bool mEnabled = true;
	SystemClock::Enum mPrecision = SystemClock::Seconds;
	QTimer timer;
	QDateTime currentTime;
	QDateTime targetTime;

	[[nodiscard]] QDateTime wallClockNow() const;
	void update();
	void setTime(const QDateTime& targetTime);
	void schedule(const QDateTime& targetTime);

#ifdef QS_TEST
public:
	/// Test-only wall-clock seam. Production builds always call QDateTime::currentDateTime().
	/// Not exposed to QML; not a second business clock.
	using NowProvider = std::function<QDateTime()>;
	void setNowProvider(NowProvider provider);
	void clearNowProvider();
	/// Drive the internal precision timer as if it fired (test only).
	void testFireTimeout();

private:
	NowProvider mNowProvider;
#endif
};
