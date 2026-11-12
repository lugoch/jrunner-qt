#include "app/app_store.h"
#include "app/main_window.h"
#include "core/app_config.h"
#include "core/debug_log.h"
#include "ui/theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifndef JRUNNER_VERSION
#define JRUNNER_VERSION "dev"
#endif

namespace {

// 未捕获异常写日志,便于排查(不阻止崩溃,但留下线索)
void writeCrashLog(const QString &reason)
{
    const QString dir = jrunner::AppConfig::configDir();
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/crash.log"));
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream << QStringLiteral("[%1] %2\n\n")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate), reason);
}

void terminateHandler()
{
    writeCrashLog(QStringLiteral("std::terminate: %1")
                      .arg(QString::fromStdString(std::current_exception()
                              ? "uncaught exception"
                              : "unknown")));
    std::abort();
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtFatalMsg) {
        writeCrashLog(QStringLiteral("Qt fatal: %1 (%2:%3)")
                          .arg(message, QString::fromLatin1(context.file),
                               QString::number(context.line)));
    }
    if (jrunner::debugLogEnabled()) {
        std::fprintf(stderr, "[qt-%d] %s\n", static_cast<int>(type), qUtf8Printable(message));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    std::set_terminate(terminateHandler);
    qInstallMessageHandler(messageHandler);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("jrunner"));
    QApplication::setApplicationName(QStringLiteral("jrunner"));
    QApplication::setApplicationVersion(QStringLiteral(JRUNNER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("jrunner"));

    jrunner::theme::applyTheme(jrunner::theme::storedThemeMode());

    jrunner::AppStore store;
    jrunner::MainWindow window(&store);
    window.show();

    // 窗口就绪后恢复项目快照;首次启动或缓存失效时才扫描。
    QTimer::singleShot(0, &store, &jrunner::AppStore::initializeProjects);

    // 冒烟测试支持:JRUNNER_SMOKE_SHOT=<path> 截图后退出
    const QString smokeShot = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("JRUNNER_SMOKE_SHOT"));
    if (!smokeShot.isEmpty()) {
        QTimer::singleShot(2500, &window, [&window, smokeShot]() {
            window.grab().save(smokeShot);
            QApplication::quit();
        });
    }

    return app.exec();
}
