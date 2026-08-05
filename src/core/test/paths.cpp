#include <cerrno>
#include <cstdio>
#include <cstdlib>

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qtemporarydir.h>
#include <qtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../instanceinfo.hpp"
#include "../paths.hpp"

namespace {

class RuntimeFixture {
public:
	RuntimeFixture()
	    : previousRuntime(qgetenv("XDG_RUNTIME_DIR"))
	    , hadRuntime(qEnvironmentVariableIsSet("XDG_RUNTIME_DIR"))
	    , previousInstance(InstanceInfo::CURRENT) {
		qputenv("XDG_RUNTIME_DIR", this->runtime.path().toUtf8());
	}

	~RuntimeFixture() {
		InstanceInfo::CURRENT = this->previousInstance;
		if (this->hadRuntime) qputenv("XDG_RUNTIME_DIR", this->previousRuntime);
		else qunsetenv("XDG_RUNTIME_DIR");
	}

	void setRuntime(const QString& path) { qputenv("XDG_RUNTIME_DIR", path.toUtf8()); }

	void init(
	    const QString& shellId = QStringLiteral("shell"),
	    const QString& pathId = QStringLiteral("path")
	) {
		QsPaths::init(shellId, pathId, {}, {}, {});
	}

	QTemporaryDir runtime;

private:
	QByteArray previousRuntime;
	bool hadRuntime;
	InstanceInfo previousInstance;
};

bool makeFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

bool linkTargets(const QString& linkPath, const QString& targetPath) {
	const QFileInfo target(targetPath);
	if (!target.isDir()) return false;

	const auto targetCanonical = QDir(targetPath).canonicalPath();
	if (targetCanonical.isEmpty()) return false;

	const QFileInfo link(linkPath);
	if (!link.isSymLink()) return false;

	const auto linkTarget = link.symLinkTarget();
	if (linkTarget.isEmpty()) return false;

	const auto linkCanonical = QDir(linkTarget).canonicalPath();
	return !linkCanonical.isEmpty() && linkCanonical == targetCanonical;
}

template <typename Scenario>
void runIsolated(Scenario scenario) {
	std::fflush(nullptr);
	const auto pid = fork();
	QVERIFY2(pid >= 0, qPrintable(QStringLiteral("fork failed: %1").arg(qt_error_string(errno))));

	if (pid == 0) {
		scenario();
		std::fflush(nullptr);
		_exit(QTest::currentTestFailed() ? EXIT_FAILURE : EXIT_SUCCESS);
	}

	int status = 0;
	pid_t result;
	do {
		result = waitpid(pid, &status, 0);
	} while (result == -1 && errno == EINTR);

	QVERIFY2(
	    result == pid,
	    qPrintable(QStringLiteral("waitpid failed: %1").arg(qt_error_string(errno)))
	);
	if (WIFSIGNALED(status)) {
		QFAIL(qPrintable(
		    QStringLiteral("isolated scenario terminated by signal %1").arg(WTERMSIG(status))
		));
	}

	QVERIFY(WIFEXITED(status));
	QCOMPARE(WEXITSTATUS(status), EXIT_SUCCESS);
}

} // namespace

class TestQsPaths: public QObject {
	Q_OBJECT

private slots:
	void baseFailureIsStickyAndSafe();
	void basePermissionFailureIsStickyAndSafe();
	void instanceFailureIsStickyAndPreservesIndependentPathLink();
	void shellFailureIsStickyAndKeepsPidLinkOnly();
	void combinedShellAndInstanceFailureCreatesNoLinks();
	void linkParentFailuresDoNotCreateWrongLinks();
	void successPathCreatesAndRecreatesAllLinks();
};

void TestQsPaths::baseFailureIsStickyAndSafe() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		const auto blockedRuntime = QDir(fixture.runtime.path()).filePath("runtime-file");
		QVERIFY(makeFile(blockedRuntime));
		fixture.setRuntime(blockedRuntime);
		fixture.init();

		QVERIFY(QsPaths::instance()->baseRunDir() == nullptr);
		QVERIFY(QFile::remove(blockedRuntime));
		QVERIFY(QDir().mkpath(blockedRuntime));
		QVERIFY(QsPaths::instance()->baseRunDir() == nullptr);
		QCOMPARE(QsPaths::basePath(QStringLiteral("instance")), QString());
		QCOMPARE(QsPaths::ipcPath(QStringLiteral("instance")), QString());
		QVERIFY(QsPaths::instance()->shellRunDir() == nullptr);
		QVERIFY(QsPaths::instance()->shellVfsDir() == nullptr);
		QVERIFY(QsPaths::instance()->instanceRunDir() == nullptr);
		QVERIFY(QsPaths::instance()->instanceRunDir() == nullptr);

		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();
	});
}

void TestQsPaths::basePermissionFailureIsStickyAndSafe() {
	if (geteuid() == 0) QSKIP("permission failure injection is not reliable as root");

	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		const auto deniedRuntime = QDir(fixture.runtime.path()).filePath("denied-runtime");
		QVERIFY(QDir().mkpath(deniedRuntime));
		QVERIFY(QFile::setPermissions(deniedRuntime, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
		fixture.setRuntime(deniedRuntime);
		fixture.init();

		auto* first = QsPaths::instance()->baseRunDir();
		QVERIFY(
		    QFile::setPermissions(
		        deniedRuntime,
		        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
		    )
		);
		QVERIFY(first == nullptr);
		QVERIFY(QsPaths::instance()->baseRunDir() == nullptr);
	});
}

void TestQsPaths::instanceFailureIsStickyAndPreservesIndependentPathLink() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		fixture.init();
		InstanceInfo::CURRENT.instanceId = QStringLiteral("blocked-instance");

		auto* base = QsPaths::instance()->baseRunDir();
		QVERIFY(base != nullptr);
		const auto instancePath = base->filePath("by-id/blocked-instance");
		QVERIFY(QDir().mkpath(QFileInfo(instancePath).path()));
		QVERIFY(makeFile(instancePath));

		QVERIFY(QsPaths::instance()->instanceRunDir() == nullptr);
		QVERIFY(QFile::remove(instancePath));
		QVERIFY(QDir().mkpath(instancePath));
		QVERIFY(QsPaths::instance()->instanceRunDir() == nullptr);
		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		auto* shell = QsPaths::instance()->shellRunDir();
		QVERIFY(shell != nullptr);
		const auto pidLink = base->filePath("by-pid/" + QString::number(getpid()));
		const auto shellLink = shell->filePath("blocked-instance");
		const auto pathLink = base->filePath("by-path/path");
		QVERIFY(!QFileInfo(pidLink).isSymLink());
		QVERIFY(!QFileInfo(shellLink).isSymLink());
		QVERIFY2(linkTargets(pathLink, shell->path()), qPrintable(pathLink));
	});
}

void TestQsPaths::shellFailureIsStickyAndKeepsPidLinkOnly() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		fixture.init();
		InstanceInfo::CURRENT.instanceId = QStringLiteral("instance");

		auto* base = QsPaths::instance()->baseRunDir();
		QVERIFY(base != nullptr);
		const auto shellPath = base->filePath("by-shell/shell");
		QVERIFY(QDir().mkpath(QFileInfo(shellPath).path()));
		QVERIFY(makeFile(shellPath));

		QVERIFY(QsPaths::instance()->shellRunDir() == nullptr);
		QVERIFY(QFile::remove(shellPath));
		QVERIFY(QDir().mkpath(shellPath));
		QVERIFY(QsPaths::instance()->shellRunDir() == nullptr);
		auto* instance = QsPaths::instance()->instanceRunDir();
		QVERIFY(instance != nullptr);
		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		const auto pidLink = base->filePath("by-pid/" + QString::number(getpid()));
		const auto shellLink = base->filePath("by-shell/shell/instance");
		const auto pathLink = base->filePath("by-path/path");
		QVERIFY2(linkTargets(pidLink, instance->path()), qPrintable(pidLink));
		QVERIFY(!QFileInfo(shellLink).isSymLink());
		QVERIFY(!QFileInfo(pathLink).isSymLink());
	});
}

void TestQsPaths::combinedShellAndInstanceFailureCreatesNoLinks() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		fixture.init();
		InstanceInfo::CURRENT.instanceId = QStringLiteral("blocked-instance");

		auto* base = QsPaths::instance()->baseRunDir();
		QVERIFY(base != nullptr);
		const auto shellPath = base->filePath("by-shell/shell");
		const auto instancePath = base->filePath("by-id/blocked-instance");
		QVERIFY(QDir().mkpath(QFileInfo(shellPath).path()));
		QVERIFY(QDir().mkpath(QFileInfo(instancePath).path()));
		QVERIFY(makeFile(shellPath));
		QVERIFY(makeFile(instancePath));

		QVERIFY(QsPaths::instance()->shellRunDir() == nullptr);
		QVERIFY(QsPaths::instance()->instanceRunDir() == nullptr);
		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		QVERIFY(!QFileInfo(base->filePath("by-pid/" + QString::number(getpid()))).isSymLink());
		QVERIFY(!QFileInfo(base->filePath("by-shell/shell/blocked-instance")).isSymLink());
		QVERIFY(!QFileInfo(base->filePath("by-path/path")).isSymLink());
	});
}

void TestQsPaths::linkParentFailuresDoNotCreateWrongLinks() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		fixture.init();
		InstanceInfo::CURRENT.instanceId = QStringLiteral("instance");

		auto* base = QsPaths::instance()->baseRunDir();
		auto* shell = QsPaths::instance()->shellRunDir();
		auto* instance = QsPaths::instance()->instanceRunDir();
		QVERIFY(base != nullptr);
		QVERIFY(shell != nullptr);
		QVERIFY(instance != nullptr);

		const auto pidParent = base->filePath("by-pid");
		const auto pathParent = base->filePath("by-path");
		QVERIFY(makeFile(pidParent));
		QVERIFY(makeFile(pathParent));

		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		const auto shellLink = shell->filePath("instance");
		QVERIFY2(linkTargets(shellLink, instance->path()), qPrintable(shellLink));
		QVERIFY(QFileInfo(pidParent).isFile());
		QVERIFY(!QFileInfo(pidParent).isSymLink());
		QVERIFY(QFileInfo(pathParent).isFile());
		QVERIFY(!QFileInfo(pathParent).isSymLink());
	});
}

void TestQsPaths::successPathCreatesAndRecreatesAllLinks() {
	runIsolated([] {
		RuntimeFixture fixture;
		QVERIFY(fixture.runtime.isValid());
		fixture.init();
		InstanceInfo::CURRENT.instanceId = QStringLiteral("instance");

		auto* base = QsPaths::instance()->baseRunDir();
		auto* shell = QsPaths::instance()->shellRunDir();
		auto* instance = QsPaths::instance()->instanceRunDir();
		QVERIFY(base != nullptr);
		QVERIFY(shell != nullptr);
		QVERIFY(instance != nullptr);
		QVERIFY(QFileInfo(instance->path()).isDir());
		QCOMPARE(QsPaths::basePath(QStringLiteral("instance")), instance->path());
		QCOMPARE(
		    QsPaths::ipcPath(QStringLiteral("instance")),
		    QDir(instance->path()).filePath("ipc.sock")
		);

		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		const auto pidLink = base->filePath("by-pid/" + QString::number(getpid()));
		const auto shellLink = shell->filePath("instance");
		const auto pathLink = base->filePath("by-path/path");
		QVERIFY2(linkTargets(pidLink, instance->path()), qPrintable(pidLink));
		QVERIFY2(linkTargets(shellLink, instance->path()), qPrintable(shellLink));
		QVERIFY2(linkTargets(pathLink, shell->path()), qPrintable(pathLink));

		QVERIFY(QFile::remove(pidLink));
		QVERIFY(makeFile(pidLink));
		QVERIFY(QFile::remove(shellLink));
		QVERIFY(QFile::link(fixture.runtime.path(), shellLink));
		QVERIFY(QFile::remove(pathLink));
		QVERIFY(makeFile(pathLink));

		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();
		QsPaths::instance()->linkRunDir();
		QsPaths::instance()->linkPathDir();

		QVERIFY2(linkTargets(pidLink, instance->path()), qPrintable(pidLink));
		QVERIFY2(linkTargets(shellLink, instance->path()), qPrintable(shellLink));
		QVERIFY2(linkTargets(pathLink, shell->path()), qPrintable(pathLink));
	});
}

QTEST_APPLESS_MAIN(TestQsPaths);

#include "paths.moc"
