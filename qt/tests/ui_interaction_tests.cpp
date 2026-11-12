#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocalSocket>
#include <QMap>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>

#include <memory>

#include "app/app_store.h"
#include "app/main_window.h"
#include "app/mcp_control_server.h"
#include "core/project_cache.h"
#include "ui/file_browser.h"
#include "ui/full_log_dialog.h"
#include "ui/log_console.h"
#include "ui/module_panel.h"
#include "ui/project_sidebar.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"

using namespace jrunner;

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool writeMavenProject(const QString &root)
{
    return writeFile(root + QStringLiteral("/pom.xml"),
                     "<project><modules><module>alpha</module><module>beta</module></modules></project>")
        && writeFile(root + QStringLiteral("/alpha/pom.xml"),
                     "<project><artifactId>alpha</artifactId></project>")
        && writeFile(root + QStringLiteral("/beta/pom.xml"),
                     "<project><artifactId>beta</artifactId></project>");
}

Project cachedProject(const QString &path)
{
    Project project;
    project.path = path;
    project.name = QStringLiteral("cached-project");
    project.language = ProjectLanguage::Mixed;
    project.requiredJava = 17;
    project.hasRequiredJava = true;
    project.hasWrapper = true;
    for (const QString &name : {QStringLiteral("alpha"), QStringLiteral("beta")}) {
        Module module;
        module.path = QLatin1Char(':') + name;
        module.name = name;
        module.absolutePath = path + QLatin1Char('/') + name;
        module.language = name == QLatin1String("alpha") ? QStringLiteral("Java") : QStringLiteral("Kotlin");
        module.isSpringBoot = true;
        module.hasJib = true;
        module.javaVersion = 17;
        module.availableProfiles = {QStringLiteral("dev"), QStringLiteral("test")};
        module.defaultProfile = QStringLiteral("dev");
        module.hasDefaultProfile = true;
        project.modules.append(module);
    }
    return project;
}

bool drainWorkers()
{
    // Deliver finished watchers too: their slots can schedule another Git worker.
    for (int pass = 0; pass < 3; ++pass) {
        if (!QThreadPool::globalInstance()->waitForDone(15000))
            return false;
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    return QThreadPool::globalInstance()->activeThreadCount() == 0;
}

QPushButton *buttonWithText(QWidget *parent, const QString &text)
{
    for (QPushButton *button : parent->findChildren<QPushButton *>()) {
        if (button->isVisible() && button->text() == text)
            return button;
    }
    return nullptr;
}

QPushButton *buttonWithToolTip(QWidget *parent, const QString &text)
{
    for (QPushButton *button : parent->findChildren<QPushButton *>()) {
        if (button->toolTip().contains(text))
            return button;
    }
    return nullptr;
}

QComboBox *visiblePicker(QWidget *parent, const QString &accessibleName)
{
    for (QComboBox *picker : parent->findChildren<QComboBox *>()) {
        if (picker->isVisible() && picker->accessibleName() == accessibleName)
            return picker;
    }
    return nullptr;
}

QJsonObject controlRequest(QLocalSocket &socket, const QString &method, const QJsonObject &params)
{
    QJsonObject req;
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("method"), method);
    req.insert(QStringLiteral("params"), params);
    socket.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    // 服务端与本测试同线程:必须 spin 事件循环,阻塞 wait 会饿死服务端
    QElapsedTimer timer;
    timer.start();
    while (!socket.canReadLine() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!socket.canReadLine())
        return QJsonObject();
    return QJsonDocument::fromJson(socket.readLine()).object();
}

QJsonObject bridgeRequest(QProcess &proc, const QJsonObject &req)
{
    proc.write(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');
    QElapsedTimer timer;
    timer.start();
    while (!proc.canReadLine() && timer.elapsed() < 8000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!proc.canReadLine())
        return QJsonObject();
    return QJsonDocument::fromJson(proc.readLine()).object();
}

bool git(const QString &root, const QStringList &arguments, QByteArray *output = nullptr)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Never inherit a GIT_DIR/worktree/index pointing outside this fixture.
    for (const QString &key : environment.keys()) {
        if (key.startsWith(QLatin1String("GIT_")))
            environment.remove(key);
    }
    environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), QStringLiteral("/dev/null"));
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    for (const QString &role : {QStringLiteral("AUTHOR"), QStringLiteral("COMMITTER")}) {
        environment.insert(QStringLiteral("GIT_%1_NAME").arg(role), QStringLiteral("UI fixture"));
        environment.insert(QStringLiteral("GIT_%1_EMAIL").arg(role), QStringLiteral("ui@example.invalid"));
    }
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(root);
    process.start(QStandardPaths::findExecutable(QStringLiteral("git")),
                  QStringList{QStringLiteral("-C"), root} + arguments);
    if (!process.waitForFinished(10000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        qWarning().noquote() << process.errorString() << process.readAllStandardError();
        return false;
    }
    if (output)
        *output = process.readAllStandardOutput().trimmed();
    return true;
}

bool createGitRepository(const QString &root, QByteArray &commit)
{
    QByteArray tree;
    return git(root, {QStringLiteral("init"), QStringLiteral("--quiet"),
                      QStringLiteral("--initial-branch=main"), QStringLiteral("--template=")})
        && git(root, {QStringLiteral("write-tree")}, &tree)
        && git(root, {QStringLiteral("commit-tree"), QString::fromLatin1(tree),
                      QStringLiteral("-m"), QStringLiteral("UI test fixture")}, &commit)
        && git(root, {QStringLiteral("update-ref"), QStringLiteral("refs/heads/main"),
                      QString::fromLatin1(commit)})
        && git(root, {QStringLiteral("update-ref"), QStringLiteral("refs/heads/feature"),
                      QString::fromLatin1(commit)});
}

} // namespace

class UiInteractionTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_config;
    QByteArray m_previousConfig;

private slots:
    void init()
    {
        m_previousConfig = qgetenv("XDG_CONFIG_HOME");
        m_config = std::make_unique<QTemporaryDir>();
        QVERIFY(m_config->isValid());
        qputenv("XDG_CONFIG_HOME", m_config->path().toUtf8());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_config->path());
        QSettings settings;
        QVERIFY(settings.fileName().startsWith(m_config->path() + QLatin1Char('/')));
        QVERIFY(ProjectCache::path().startsWith(m_config->path() + QLatin1Char('/')));
        settings.clear();
        // No test may fall back to scanning the user's default project directories.
        settings.setValue(QStringLiteral("scan/roots"), QStringList{});
        settings.sync();
        QVERIFY(!QFileInfo::exists(ProjectCache::path()));
    }

    void cleanup()
    {
        const bool finished = drainWorkers();
        {
            QSettings settings;
            settings.clear();
            settings.sync();
        }
        QFile::remove(ProjectCache::path());
        qputenv("XDG_CONFIG_HOME", m_previousConfig);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                           QString::fromUtf8(m_previousConfig));
        m_config.reset();
        QVERIFY2(finished, "Background workers must finish before removing test configuration");
    }

    void cachedProjectsRestoreWithoutScanning_data()
    {
        QTest::addColumn<bool>("removeDirectory");
        QTest::newRow("missing-build-file") << false;
        QTest::newRow("missing-project-directory") << true;
    }

    void cachedProjectsRestoreWithoutScanning()
    {
        QFETCH(bool, removeDirectory);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath(QStringLiteral("project"));
        QVERIFY(writeFile(path + QStringLiteral("/build.gradle"), "plugins { id 'java' }"));
        const Project project = cachedProject(path);
        Project other = cachedProject(root.filePath(QStringLiteral("other")));
        other.name = QStringLiteral("other-project");
        const QString selectedModule = project.modules.last().path;
        QVERIFY(ProjectCache::save({{other, project}, path, selectedModule}));
        if (removeDirectory)
            QVERIFY(QDir(path).removeRecursively());
        else
            QVERIFY(QFile::remove(path + QStringLiteral("/build.gradle")));

        AppStore store;
        store.setScanRoots({root.path()});
        QSignalSpy scanning(&store, &AppStore::isScanningChanged);
        store.initializeProjects();
        store.initializeProjects();
        QCOMPARE(scanning.count(), 0);
        QVERIFY(drainWorkers());
        QCOMPARE(scanning.count(), 0);
        QVERIFY(!store.isScanning());
        QCOMPARE(store.projects().size(), 2);
        QCOMPARE(store.projects().first().toJson(), other.toJson());
        QCOMPARE(store.projects().last().toJson(), project.toJson());
        QCOMPARE(store.selectedProjectPath(), path);
        QCOMPARE(store.selectedModulePath(), selectedModule);
        QVERIFY(store.selectedModule().has_value());
        QCOMPARE(store.selectedModule()->toJson(), project.modules.last().toJson());
    }

    void validEmptyCacheDoesNotRescan()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        QVERIFY(ProjectCache::save(ProjectSnapshot{}));
        AppStore store;
        store.setScanRoots({root.path()});
        QSignalSpy scanning(&store, &AppStore::isScanningChanged);
        store.initializeProjects();
        store.initializeProjects();
        QVERIFY(drainWorkers());
        QCOMPARE(scanning.count(), 0);
        QVERIFY(!store.isScanning());
        QVERIFY(store.projects().isEmpty());
        QVERIFY(store.selectedProjectPath().isEmpty());
        QVERIFY(store.selectedModulePath().isEmpty());
    }

    void firstInitializeScansOnceAndRestartUsesCache_data()
    {
        QTest::addColumn<bool>("invalidCache");
        QTest::newRow("missing-cache") << false;
        QTest::newRow("invalid-cache") << true;
    }

    void firstInitializeScansOnceAndRestartUsesCache()
    {
        QFETCH(bool, invalidCache);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        if (invalidCache)
            QVERIFY(writeFile(ProjectCache::path(), "{invalid json"));
        Project expected;
        QString selectedModule;
        {
            AppStore store;
            store.setScanRoots({root.path()});
            QSignalSpy scanning(&store, &AppStore::isScanningChanged);
            store.initializeProjects();
            QVERIFY(store.isScanning());
            QCOMPARE(scanning.count(), 1);
            store.initializeProjects();
            QVERIFY(drainWorkers());
            QVERIFY(!store.isScanning());
            QCOMPARE(scanning.count(), 2); // One start and one finish, not two scans.
            QCOMPARE(store.projects().size(), 1);
            expected = store.projects().first();
            QCOMPARE(expected.modules.size(), 2);
            selectedModule = expected.modules.last().path;
            store.selectModule(selectedModule);
            store.initializeProjects();
            QVERIFY(drainWorkers());
            QCOMPARE(scanning.count(), 2);
        }
        const auto snapshot = ProjectCache::load();
        QVERIFY(snapshot.has_value());
        QCOMPARE(snapshot->projects.size(), 1);
        QCOMPARE(snapshot->selectedModulePath, selectedModule);
        QVERIFY(QFile::remove(root.filePath(QStringLiteral("pom.xml"))));
        QVERIFY(QFile::remove(root.filePath(QStringLiteral("alpha/pom.xml"))));
        QVERIFY(QFile::remove(root.filePath(QStringLiteral("beta/pom.xml"))));

        AppStore restarted;
        QSignalSpy scanning(&restarted, &AppStore::isScanningChanged);
        restarted.initializeProjects();
        QVERIFY(drainWorkers());
        QCOMPARE(scanning.count(), 0);
        QCOMPARE(restarted.projects().size(), 1);
        QCOMPARE(restarted.projects().first().toJson(), expected.toJson());
        QCOMPARE(restarted.selectedProjectPath(), root.path());
        QCOMPARE(restarted.selectedModulePath(), selectedModule);
    }

    void addProjectPersistsWithoutStartupScan()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        QString selectedModule;
        {
            AppStore store;
            store.addProject(root.path());
            QVERIFY(drainWorkers());
            QCOMPARE(store.projects().size(), 1);
            const auto added = ProjectCache::load();
            QVERIFY(added.has_value());
            QCOMPARE(added->projects.size(), 1);
            QCOMPARE(added->projects.first().toJson(), store.projects().first().toJson());
            QCOMPARE(added->selectedProjectPath, root.path());
            selectedModule = store.projects().first().modules.last().path;
            store.selectModule(selectedModule);
        }
        AppStore restarted;
        QSignalSpy scanning(&restarted, &AppStore::isScanningChanged);
        restarted.initializeProjects();
        QVERIFY(drainWorkers());
        QCOMPARE(scanning.count(), 0);
        QCOMPARE(restarted.manualProjects(), QStringList{root.path()});
        QCOMPARE(restarted.selectedProjectPath(), root.path());
        QCOMPARE(restarted.selectedModulePath(), selectedModule);
    }

    void savedEmptyScanRootsAreRetained()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        {
            AppStore store;
            store.setScanRoots({root.path()});
            store.setScanRoots({});
            QVERIFY(drainWorkers());
        }
        QSettings settings;
        QVERIFY(settings.contains(QStringLiteral("scan/roots")));
        QVERIFY(settings.value(QStringLiteral("scan/roots")).toStringList().isEmpty());
        AppStore restarted;
        QVERIFY(restarted.scanRoots().isEmpty());
    }

    void removalInvalidatesPendingCompletion_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("add") << QStringLiteral("add");
        QTest::newRow("rescan-project") << QStringLiteral("rescan");
        QTest::newRow("scan-all") << QStringLiteral("scan");
    }

    void removalInvalidatesPendingCompletion()
    {
        QFETCH(QString, operation);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        AppStore store;
        store.setScanRoots({root.path()});
        if (operation != QLatin1String("add")) {
            store.addProject(root.path());
            QVERIFY(drainWorkers());
            QVERIFY(store.projectAt(root.path()).has_value());
        }
        if (operation == QLatin1String("add"))
            store.addProject(root.path());
        else if (operation == QLatin1String("rescan"))
            store.rescanProject(root.path());
        else
            store.scanProjects();
        // Do not process events between request and removal. Even a fast worker's
        // finished notification must still be pending when the tombstone is set.
        store.removeProject(root.path());
        QVERIFY(drainWorkers());
        QVERIFY(!store.isScanning());
        QVERIFY(store.projects().isEmpty());
        QVERIFY(store.manualProjects().isEmpty());
        QVERIFY(store.selectedProjectPath().isEmpty());
        QVERIFY(store.selectedModulePath().isEmpty());
        const auto snapshot = ProjectCache::load();
        QVERIFY(snapshot.has_value());
        QVERIFY(snapshot->projects.isEmpty());
        QVERIFY(snapshot->selectedProjectPath.isEmpty());
        QVERIFY(snapshot->selectedModulePath.isEmpty());
    }

    void destructionWithPendingWorkers_data()
    {
        removalInvalidatesPendingCompletion_data();
    }

    void destructionWithPendingWorkers()
    {
        QFETCH(QString, operation);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        const Project project = cachedProject(root.path());
        QVERIFY(ProjectCache::save({{project}, project.path, project.modules.first().path}));
        auto store = std::make_unique<AppStore>();
        QPointer<AppStore> guard(store.get());
        {
            MainWindow window(store.get());
            store->setScanRoots({root.path()});
            store->initializeProjects();
            if (operation == QLatin1String("add"))
                store->addProject(root.path());
            else if (operation == QLatin1String("rescan"))
                store->rescanProject(root.path());
            else
                store->scanProjects();
            store->git()->loadBranches(root.path());
        }
        store.reset();
        QVERIFY(guard.isNull());
        QVERIFY2(drainWorkers(), "Destroying the window/store must not leave unsafe worker callbacks");
    }

    void moduleSelectionPreservesEventReceiver()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QFile pom(project.filePath(QStringLiteral("pom.xml")));
        QVERIFY(pom.open(QIODevice::WriteOnly));
        pom.write("<project><modules><module>alpha</module><module>beta</module></modules></project>");
        pom.close();
        for (const QString &name : {QStringLiteral("alpha"), QStringLiteral("beta")}) {
            QVERIFY(QDir(project.path()).mkdir(name));
            QFile module(project.filePath(name + QStringLiteral("/pom.xml")));
            QVERIFY(module.open(QIODevice::WriteOnly));
            module.write("<project><artifactId>" + name.toUtf8() + "</artifactId></project>");
        }

        AppStore store;
        ModulePanel panel(&store);
        panel.resize(700, 600);
        panel.show();
        store.addProject(project.path());
        QTRY_VERIFY(store.selectedModule().has_value());
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        for (int i = 0; i < 12; ++i) {
            const QString name = i % 2 == 0 ? QStringLiteral("beta") : QStringLiteral("alpha");
            QPointer<QWidget> row;
            for (QWidget *widget : panel.findChildren<QWidget *>()) {
                if (!widget->isVisible() || !widget->property("jrCard").toBool())
                    continue;
                for (QLabel *label : widget->findChildren<QLabel *>()) {
                    if (label->text() == name)
                        row = widget;
                }
            }
            QVERIFY(row);
            QTest::mousePress(row, Qt::LeftButton, Qt::NoModifier, row->rect().center());
            QVERIFY2(row, "The mouse event receiver must survive synchronous selection signals");
            QVERIFY(store.selectedModule().has_value());
            QCOMPARE(store.selectedModule()->name, name);
            QVERIFY(!row->isVisible());
            QTest::mouseRelease(row, Qt::LeftButton, Qt::NoModifier, row->rect().center());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY2(!row, "Retired rows must be destroyed after the event returns");
            QCoreApplication::processEvents();

            QComboBox *picker = nullptr;
            for (QComboBox *combo : panel.findChildren<QComboBox *>()) {
                if (combo->isVisible() && combo->findData(store.selectedModulePath()) >= 0)
                    picker = combo;
            }
            QVERIFY(picker);
            QCOMPARE(picker->currentData().toString(), store.selectedModulePath());
        }
        QThreadPool::globalInstance()->waitForDone();
    }

    void branchMenuOnlyOpensOnExplicitClick()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("Git is required for the temporary repository fixture");
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(writeMavenProject(root.path()));
        QByteArray commit;
        QVERIFY(createGitRepository(root.path(), commit));

        AppStore store;
        MainWindow window(&store);
        auto *button = buttonWithToolTip(&window, QStringLiteral("点击切换分支"));
        QVERIFY(button);
        auto *menu = button->findChild<QMenu *>();
        QVERIFY(menu);
        QSignalSpy shown(menu, &QMenu::aboutToShow);
        QSignalSpy branches(store.git(), &GitMonitor::branchesUpdated);
        QSignalSpy checkedOut(store.git(), &GitMonitor::checkoutSucceeded);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        store.addProject(root.path());
        QVERIFY(drainWorkers());
        QCOMPARE(store.selectedProjectPath(), root.path());
        QCOMPARE(store.git()->branches(root.path()),
                 (QStringList{QStringLiteral("feature"), QStringLiteral("main")}));
        QVERIFY(!branches.isEmpty());
        QVERIFY(button->isVisible());
        QVERIFY(!menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(shown.count(), 0);

        store.selectProject(root.path());
        store.selectModule(store.projects().first().modules.last().path);
        QVERIFY(drainWorkers());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(shown.count(), 0);
        const QString status = store.statusMessage();
        QSignalSpy selection(&store, &AppStore::selectionChanged);
        branches.clear();
        QVERIFY(git(root.path(), {QStringLiteral("update-ref"), QStringLiteral("refs/heads/topic"),
                                  QString::fromLatin1(commit)}));
        store.git()->loadBranches(root.path());
        QVERIFY(drainWorkers());
        QCOMPARE(branches.count(), 1);
        QCOMPARE(selection.count(), 0); // A list refresh is not a checkout/rescan.
        QCOMPARE(checkedOut.count(), 0);
        QCOMPARE(store.statusMessage(), status);
        QVERIFY(!menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(shown.count(), 0);

        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(menu->isVisible());
        QTRY_VERIFY(QApplication::activePopupWidget() == menu);
        QCOMPARE(shown.count(), 1);
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(!menu->isVisible());
        QTRY_VERIFY(!QApplication::activePopupWidget());

        // Reproduce a late queued branch notification after dismissing the menu.
        QTimer::singleShot(0, store.git(), [&store, path = root.path()]() {
            emit store.git()->branchesUpdated(path);
        });
        QVERIFY(drainWorkers());
        QVERIFY(!menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(shown.count(), 1);
        QCOMPARE(selection.count(), 0);
        QCOMPARE(store.statusMessage(), status);

        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(QApplication::activePopupWidget() == menu);
        QAction *feature = nullptr;
        for (QAction *action : menu->actions()) {
            if (action->text() == QLatin1String("feature"))
                feature = action;
        }
        QVERIFY(feature);
        menu->hide();
        feature->trigger();
        QVERIFY(drainWorkers());
        QCOMPARE(checkedOut.count(), 1);
        QCOMPARE(checkedOut.first().at(0).toString(), root.path());
        QCOMPARE(checkedOut.first().at(1).toString(), QStringLiteral("feature"));
        QCOMPARE(store.git()->status(root.path()).branch, QStringLiteral("feature"));
        QCOMPARE(store.statusMessage(), QStringLiteral("已切换到 feature"));
        QVERIFY(!menu->isVisible());
        QVERIFY(!QApplication::activePopupWidget());
        QCOMPARE(shown.count(), 2);
    }

    void sidebarSettingsOpensAndCanBeRejected()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        AppStore store;
        store.setScanRoots({root.path()});
        MainWindow window(&store);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *sidebar = window.findChild<ProjectSidebar *>();
        QVERIFY(sidebar);
        auto *button = buttonWithToolTip(sidebar, QStringLiteral("设置"));
        QVERIFY(button);
        QSignalSpy requested(sidebar, &ProjectSidebar::openSettingsRequested);
        QSignalSpy scanning(&store, &AppStore::isScanningChanged);
        bool inspected = false;
        bool rootsMatch = false;
        QTimer inspector;
        inspector.setInterval(0);
        connect(&inspector, &QTimer::timeout, &window, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            inspected = qobject_cast<SettingsDialog *>(dialog) != nullptr;
            if (auto *roots = dialog->findChild<QPlainTextEdit *>())
                rootsMatch = roots->toPlainText() == root.path();
            // Reject before making assertions, even if this is the wrong dialog.
            dialog->reject();
            inspector.stop();
        });
        // Also close a visible child dialog if it never becomes active on Wayland.
        QTimer watchdog;
        watchdog.setInterval(2000);
        connect(&watchdog, &QTimer::timeout, &window, [&window]() {
            for (QDialog *dialog : window.findChildren<QDialog *>())
                dialog->reject();
        });
        inspector.start();
        watchdog.start();
        QTest::mouseClick(button, Qt::LeftButton);
        inspector.stop();
        watchdog.stop();
        QCOMPARE(requested.count(), 1);
        QVERIFY(inspected);
        QVERIFY(rootsMatch);
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(scanning.count(), 0);
        QCOMPARE(store.scanRoots(), QStringList{root.path()});
    }

    void topRunButtonStopsProcessAndReattachesLogsOnRestart()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString wrapper = root.filePath(QStringLiteral("gradlew"));
        QVERIFY(writeFile(wrapper, "#!/bin/sh\nprintf '%s\\n' \"$JRUNNER_TEST_MARKER\"\nexec /bin/sleep 60\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner));
        const Project project = cachedProject(root.path());
        const Module module = project.modules.last();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        auto *panel = window.findChild<ModulePanel *>();
        auto *console = window.findChild<LogConsole *>();
        QVERIFY(panel);
        QVERIFY(console);
        auto *log = console->findChild<QPlainTextEdit *>(QStringLiteral("jrLogView"));
        QVERIFY(log);
        QPushButton *topButton = nullptr;
        for (QPushButton *button : window.findChildren<QPushButton *>()) {
            if (button->isVisible() && button->text().contains(QStringLiteral("启动服务")))
                topButton = button;
        }
        QVERIFY(topButton);
        QVERIFY(topButton->isEnabled());
        const QString key = AppStore::taskKey(project.path, module.path);
        QSignalSpy activeTask(&store, &AppStore::activeTaskChanged);
        QPointer<ProcessTask> previous;
        for (int run = 1; run <= 2; ++run) {
            const QString marker = QStringLiteral("ui-fixture-run-%1").arg(run);
            ModuleConfig config;
            config.profile = QStringLiteral("test");
            config.envVars.insert(QStringLiteral("JRUNNER_TEST_MARKER"), marker);
            store.updateModuleConfig(project.path, module.path, config);
            activeTask.clear();
            QTest::mouseClick(topButton, Qt::LeftButton);
            ProcessTask *task = store.activeTask();
            QVERIFY(task);
            QVERIFY(task != previous.data());
            QCOMPARE(task->command().executable, wrapper);
            QCOMPARE(store.activeTaskKey(), key);
            QCOMPARE(activeTask.count(), 1); // Same key, new task must still notify the console.
            QTRY_COMPARE(task->state().kind, TaskState::Running);
            QTRY_VERIFY(log->toPlainText().contains(marker));
            if (run == 2)
                QVERIFY(!log->toPlainText().contains(QStringLiteral("ui-fixture-run-1")));
            QVERIFY(topButton->text().contains(QStringLiteral("停止")));
            QCOMPARE(topButton->property("role").toString(), QStringLiteral("danger"));
            QVERIFY(buttonWithText(panel, QStringLiteral("停止")));
            auto *picker = visiblePicker(panel, QStringLiteral("模块"));
            QVERIFY(picker);
            QCOMPARE(picker->currentData().toString(), module.path);
            QVERIFY(!picker->itemIcon(picker->currentIndex()).isNull());
            auto *profile = visiblePicker(panel, QStringLiteral("Spring Profile"));
            QVERIFY(profile);
            QCOMPARE(profile->currentData().toString(), QStringLiteral("test"));

            auto *process = task->findChild<QProcess *>();
            QVERIFY(process);
            const qint64 pid = process->processId();
            QVERIFY(pid > 0);
            QSignalSpy exited(process, &QProcess::finished);
            QTest::mouseClick(topButton, Qt::LeftButton);
            QTRY_COMPARE(exited.count(), 1);
            QTRY_COMPARE(task->state().kind, TaskState::Success);
            QTRY_VERIFY(!QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)));
            QVERIFY(!store.isRunning(project.path, module.path));
            QVERIFY(topButton->text().contains(QStringLiteral("启动服务")));
            QCOMPARE(topButton->property("role").toString(), QStringLiteral("primary"));
            QTRY_VERIFY(buttonWithText(panel, QStringLiteral("启动")));
            QVERIFY(!buttonWithText(panel, QStringLiteral("停止")));
            picker = visiblePicker(panel, QStringLiteral("模块"));
            QVERIFY(picker);
            QVERIFY(picker->itemIcon(picker->currentIndex()).isNull());
            previous = task;
        }
    }

    void fullLogViewerShowsEvictedHistory()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString wrapper = root.filePath(QStringLiteral("gradlew"));
        QVERIFY(writeFile(wrapper,
            "#!/bin/sh\nprintf 'early-marker\\n'\ni=1\n"
            "while [ $i -le 120 ]; do printf 'filler-%03d\\n' \"$i\"; i=$((i+1)); done\n"
            "printf 'late-marker\\n'\nexec /bin/sleep 60\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner));
        // 小窗口迫使实时控制台驱逐早期行;全量历史仍落盘
        QVERIFY(writeFile(AppConfig::configPath(), "{\"maxLogLines\":50}"));
        const Project project = cachedProject(root.path());
        const Module module = project.modules.last();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        auto *console = window.findChild<LogConsole *>();
        QVERIFY(console);
        auto *log = console->findChild<QPlainTextEdit *>(QStringLiteral("jrLogView"));
        QVERIFY(log);
        QPushButton *topButton = nullptr;
        for (QPushButton *button : window.findChildren<QPushButton *>()) {
            if (button->isVisible() && button->text().contains(QStringLiteral("启动服务")))
                topButton = button;
        }
        QVERIFY(topButton);
        QTest::mouseClick(topButton, Qt::LeftButton);
        ProcessTask *task = store.activeTask();
        QVERIFY(task);
        QTRY_COMPARE(task->state().kind, TaskState::Running);
        QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("late-marker"))
                    && !log->toPlainText().contains(QStringLiteral("early-marker")));
        QVERIFY(task->totalLineCount() > 50);

        auto *fullButton = console->findChild<QPushButton *>(QStringLiteral("jrFullLogButton"));
        QVERIFY(fullButton);
        QVERIFY(fullButton->isEnabled());
        QTest::mouseClick(fullButton, Qt::LeftButton);
        auto *dialog = window.findChild<FullLogDialog *>();
        QVERIFY(dialog);
        auto *fullView = dialog->findChild<QPlainTextEdit *>(QStringLiteral("jrFullLogView"));
        QVERIFY(fullView);
        QTRY_VERIFY(fullView->toPlainText().contains(QStringLiteral("early-marker")));
        QVERIFY(fullView->toPlainText().contains(QStringLiteral("late-marker")));

        auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("jrFullLogSearch"));
        QVERIFY(search);
        search->setText(QStringLiteral("early-marker"));
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_VERIFY(fullView->toPlainText().contains(QStringLiteral("early-marker")));
        QVERIFY(!fullView->toPlainText().contains(QStringLiteral("filler-")));

        dialog->close();
        QTest::mouseClick(topButton, Qt::LeftButton);
        QTRY_COMPARE(task->state().kind, TaskState::Success);
        QVERIFY(!store.isRunning(project.path, module.path));
    }

    void mcpControlServerGatedAndRoundTrip()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString wrapper = root.filePath(QStringLiteral("gradlew"));
        QVERIFY(writeFile(wrapper, "#!/bin/sh\nexec /bin/sleep 60\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner));
        QVERIFY(writeFile(AppConfig::configPath(), "{\"mcpEnabled\":true}"));
        const Project project = cachedProject(root.path());
        const Module module = project.modules.first();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        AppStore store;
        store.initializeProjects();
        QVERIFY(store.mcpEnabled());
        QVERIFY(store.mcpListening());

        QLocalSocket socket;
        socket.connectToServer(McpControlServer::serverName());
        QVERIFY(socket.waitForConnected(3000));
        const QJsonObject list = controlRequest(socket, QStringLiteral("list_projects"), {});
        QVERIFY(list.value(QStringLiteral("ok")).toBool());
        QVERIFY(QString::fromUtf8(QJsonDocument(list).toJson()).contains(project.path));

        QJsonObject args;
        args.insert(QStringLiteral("project"), project.path);
        args.insert(QStringLiteral("module"), module.path);
        QVERIFY(controlRequest(socket, QStringLiteral("start_module"), args)
                    .value(QStringLiteral("ok")).toBool());
        QTRY_COMPARE(store.task(project.path, module.path)->state().kind, TaskState::Running);
        const QJsonObject status = controlRequest(socket, QStringLiteral("status"), args);
        QCOMPARE(status.value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("state")).toString(),
                 QStringLiteral("running"));
        QVERIFY(controlRequest(socket, QStringLiteral("stop_module"), args)
                    .value(QStringLiteral("ok")).toBool());
        QTRY_VERIFY(!store.isRunning(project.path, module.path));
        socket.disconnectFromServer();

        store.setMcpEnabled(false);
        QVERIFY(!store.mcpListening());
        store.setMcpEnabled(true);
        QVERIFY(store.mcpListening());
    }

    void mcpBridgeStdioRoundTrip()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString wrapper = root.filePath(QStringLiteral("gradlew"));
        QVERIFY(writeFile(wrapper, "#!/bin/sh\nexec /bin/sleep 60\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner));
        QVERIFY(writeFile(AppConfig::configPath(), "{\"mcpEnabled\":true}"));
        const Project project = cachedProject(root.path());
        const Module module = project.modules.first();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        AppStore store;
        store.initializeProjects();
        QVERIFY(store.mcpListening());

        QProcess bridge;
        bridge.setProcessChannelMode(QProcess::SeparateChannels);
        bridge.start(QCoreApplication::applicationDirPath() + QStringLiteral("/../jrunner-mcp"));
        QVERIFY(bridge.waitForStarted(3000));

        QJsonObject init;
        init.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        init.insert(QStringLiteral("id"), 1);
        init.insert(QStringLiteral("method"), QStringLiteral("initialize"));
        const QJsonObject initResp = bridgeRequest(bridge, init);
        QCOMPARE(initResp.value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("serverInfo")).toObject()
                     .value(QStringLiteral("name")).toString(),
                 QStringLiteral("jrunner-mcp"));

        QJsonObject listReq;
        listReq.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        listReq.insert(QStringLiteral("id"), 2);
        listReq.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
        const QJsonArray tools = bridgeRequest(bridge, listReq)
                                     .value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("tools")).toArray();
        bool hasStart = false;
        for (const QJsonValue &t : tools)
            hasStart |= t.toObject().value(QStringLiteral("name")).toString() == QLatin1String("start_module");
        QVERIFY(hasStart);

        QJsonObject call;
        call.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        call.insert(QStringLiteral("id"), 3);
        call.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
        QJsonObject callParams;
        callParams.insert(QStringLiteral("name"), QStringLiteral("list_projects"));
        callParams.insert(QStringLiteral("arguments"), QJsonObject{});
        call.insert(QStringLiteral("params"), callParams);
        const QJsonObject callResult = bridgeRequest(bridge, call).value(QStringLiteral("result")).toObject();
        QVERIFY(!callResult.value(QStringLiteral("isError")).toBool());
        QVERIFY(callResult.value(QStringLiteral("content")).toArray().at(0).toObject()
                    .value(QStringLiteral("text")).toString().contains(project.path));

        QJsonObject args;
        args.insert(QStringLiteral("project"), project.path);
        args.insert(QStringLiteral("module"), module.path);
        QJsonObject startCall = call;
        startCall.insert(QStringLiteral("id"), 4);
        QJsonObject startParams;
        startParams.insert(QStringLiteral("name"), QStringLiteral("start_module"));
        startParams.insert(QStringLiteral("arguments"), args);
        startCall.insert(QStringLiteral("params"), startParams);
        QVERIFY(!bridgeRequest(bridge, startCall).value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("isError")).toBool());
        QTRY_VERIFY(store.isRunning(project.path, module.path));

        QJsonObject stopCall = startCall;
        stopCall.insert(QStringLiteral("id"), 5);
        QJsonObject stopParams;
        stopParams.insert(QStringLiteral("name"), QStringLiteral("stop_module"));
        stopParams.insert(QStringLiteral("arguments"), args);
        stopCall.insert(QStringLiteral("params"), stopParams);
        bridgeRequest(bridge, stopCall);
        QTRY_VERIFY(!store.isRunning(project.path, module.path));

        bridge.kill();
        QVERIFY(bridge.waitForFinished(3000));
    }

    void stopUpdatesStatusMessageAndDotColor()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString wrapper = root.filePath(QStringLiteral("gradlew"));
        QVERIFY(writeFile(wrapper, "#!/bin/sh\nexec /bin/sleep 60\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner));
        const Project project = cachedProject(root.path());
        const Module module = project.modules.first();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        store.runModuleAt(project.path, module.path);
        QTRY_COMPARE(store.task(project.path, module.path)->state().kind, TaskState::Running);
        QVERIFY(store.statusMessage().contains(QStringLiteral("正在启动")));
        store.stopModuleAt(project.path, module.path);
        QTRY_COMPARE(store.task(project.path, module.path)->state().kind, TaskState::Success);
        QTRY_VERIFY(store.statusMessage().contains(QStringLiteral("已结束")));
        QVERIFY(!store.statusMessage().contains(QStringLiteral("正在启动")));
        TaskState done;
        done.kind = TaskState::Success;
        QCOMPARE(theme::taskStateColor(done), theme::textTertiary());
    }

    void darkModeRefreshesInlineLabelColors()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Project project = cachedProject(root.path());
        QVERIFY(ProjectCache::save({{project}, project.path, project.modules.first().path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        theme::applyTheme(theme::ThemeMode::Dark);
        QCoreApplication::processEvents();
        QLabel *title = nullptr;
        for (QLabel *l : window.findChildren<QLabel *>())
            if (l->text() == QLatin1String("JRunner"))
                title = l;
        QVERIFY(title);
        QVERIFY(title->styleSheet().contains(theme::textPrimary().name()));
        theme::applyTheme(theme::ThemeMode::System);
        QCoreApplication::processEvents();
    }

    void projectRenameAndSelectableNames()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Project project = cachedProject(root.path());
        const Module module = project.modules.first();
        QVERIFY(ProjectCache::save({{project}, project.path, module.path}));
        // 全局样式表 + 非默认字体是标签被压缩裁切的触发条件,回归测试必须覆盖
        const QFont oldFont = QApplication::font();
        QApplication::setFont(QFont(QStringLiteral("Noto Sans"), 10));
        theme::applyTheme(theme::ThemeMode::Light);
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());

        store.renameProject(project.path, QStringLiteral("我的项目"));
        QCOMPARE(store.projects().first().name, QStringLiteral("我的项目"));
        QCOMPARE(AppConfig::load().projectNames.value(project.path), QStringLiteral("我的项目"));

        bool sidebarSelectable = false, moduleSelectable = false;
        for (QLabel *l : window.findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("我的项目"))
                sidebarSelectable |= l->textInteractionFlags().testFlag(Qt::TextSelectableByMouse);
            if (l->text() == module.name)
                moduleSelectable |= l->textInteractionFlags().testFlag(Qt::TextSelectableByMouse);
        }
        QVERIFY(sidebarSelectable);
        QVERIFY(moduleSelectable);

        // 侧边栏项目名不得被裁切:实际高度 >= 字体行高
        for (QLabel *l : window.findChildren<QLabel *>()) {
            if (l->text() == QStringLiteral("我的项目") && l->parentWidget()
                && l->parentWidget()->parentWidget()) {
                QVERIFY(l->height() >= l->fontMetrics().height());
            }
        }

        store.renameProject(project.path, QString());   // 留空恢复目录名
        QCOMPARE(store.projects().first().name, QFileInfo(project.path).fileName());
        QApplication::setFont(oldFont);
        theme::applyTheme(theme::ThemeMode::System);
    }

    void rapidModuleClicksSwitchSelection()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Project project = cachedProject(root.path());
        QVERIFY(ProjectCache::save({{project}, project.path, project.modules.first().path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        auto *panel = window.findChild<ModulePanel *>();
        QVERIFY(panel);
        auto rowFor = [&](const QString &moduleName) -> QWidget * {
            for (QLabel *l : panel->findChildren<QLabel *>())
                if (l->text() == moduleName)
                    return l->parentWidget();
            return nullptr;
        };
        const QString a = project.modules.at(0).name;
        const QString b = project.modules.at(1).name;
        // 快速单击两个不同模块,第二次必须生效
        QWidget *rowA = rowFor(a);
        QVERIFY(rowA);
        QTest::mouseClick(rowA, Qt::LeftButton);
        QWidget *rowB = rowFor(b);
        QVERIFY(rowB);
        QTest::mouseClick(rowB, Qt::LeftButton);
        QCOMPARE(store.selectedModulePath(), project.modules.at(1).path);
        // 双击(快速连点被合成为 DblClick)也应选中
        QWidget *rowA2 = rowFor(a);
        QVERIFY(rowA2);
        QTest::mouseDClick(rowA2, Qt::LeftButton);
        QCOMPARE(store.selectedModulePath(), project.modules.at(0).path);
    }

    void framelessTitleBarWindowControlsWork()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Project project = cachedProject(root.path());
        QVERIFY(ProjectCache::save({{project}, project.path, project.modules.first().path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        auto *maxBtn = window.findChild<QPushButton *>(QStringLiteral("jrMaxButton"));
        auto *minBtn = window.findChild<QPushButton *>(QStringLiteral("jrMinButton"));
        auto *closeBtn = window.findChild<QPushButton *>(QStringLiteral("jrCloseButton"));
        QVERIFY(maxBtn && minBtn && closeBtn);
        QTest::mouseClick(maxBtn, Qt::LeftButton);
        QTRY_VERIFY(window.isMaximized());
        QTest::mouseClick(maxBtn, Qt::LeftButton);
        QTRY_VERIFY(!window.isMaximized());
        QTest::mouseClick(minBtn, Qt::LeftButton);
        QTRY_VERIFY(window.isMinimized());
        window.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(closeBtn, Qt::LeftButton);
        QTRY_VERIFY(!window.isVisible());
    }

    void workspaceWidthsAndShortcutsRemainUsable()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Project project = cachedProject(root.path());
        QVERIFY(ProjectCache::save({{project}, project.path, project.modules.first().path}));
        AppStore store;
        MainWindow window(&store);
        store.initializeProjects();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(drainWorkers());
        auto *splitter = window.findChild<QSplitter *>();
        QVERIFY(splitter);
        QCOMPARE(splitter->count(), 5);
        QComboBox *workspace = nullptr;
        for (QComboBox *picker : window.findChildren<QComboBox *>()) {
            if (picker->count() == 2 && picker->itemText(0) == QStringLiteral("模块")
                && picker->itemText(1) == QStringLiteral("文件"))
                workspace = picker;
        }
        QVERIFY(workspace);
        for (int index : {0, 1, 0, 1, 0}) {
            workspace->setCurrentIndex(index);
            QCoreApplication::processEvents();
            const int middle = index == 0 ? 1 : 3;
            const int right = index == 0 ? 2 : 4;
            QVERIFY(splitter->widget(middle)->isVisible());
            QVERIFY(splitter->widget(right)->isVisible());
            QVERIFY(splitter->sizes().at(0) > 0);
            QVERIFY(splitter->sizes().at(middle) > 0);
            QVERIFY(splitter->sizes().at(right) > 0);

            QMap<QString, int> shortcuts;
            const auto addShortcut = [&shortcuts](const QKeySequence &shortcut) {
                if (!shortcut.isEmpty())
                    ++shortcuts[shortcut.toString(QKeySequence::PortableText)];
            };
            for (QAction *action : window.findChildren<QAction *>()) {
                if (action->isEnabled()) {
                    for (const QKeySequence &shortcut : action->shortcuts())
                        addShortcut(shortcut);
                }
            }
            for (QAbstractButton *button : window.findChildren<QAbstractButton *>()) {
                if (button->isVisible() && button->isEnabled())
                    addShortcut(button->shortcut());
            }
            for (QShortcut *shortcut : window.findChildren<QShortcut *>()) {
                if (shortcut->isEnabled()) {
                    for (const QKeySequence &key : shortcut->keys())
                        addShortcut(key);
                }
            }
            for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it)
                QVERIFY2(it.value() == 1, qPrintable(QStringLiteral("Ambiguous shortcut: %1").arg(it.key())));
            for (const QString &key : {QStringLiteral("Ctrl+Shift+R"), QStringLiteral("Ctrl+B"),
                                       QStringLiteral("Ctrl+."), QStringLiteral("Ctrl+R")})
                QCOMPARE(shortcuts.value(key), 1);
        }
    }
};

int main(int argc, char **argv)
{
    QTemporaryDir config;
    if (!config.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", "wayland");
    // GitMonitor also launches Git: keep inherited repository/config overrides
    // from redirecting the temporary fixture's status or checkout operations.
    const auto environment = QProcessEnvironment::systemEnvironment();
    for (const QString &key : environment.keys()) {
        if (key.startsWith(QLatin1String("GIT_")))
            qunsetenv(key.toUtf8().constData());
    }
    qputenv("GIT_CONFIG_NOSYSTEM", "1");
    qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("jrunner-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("ui-interaction"));
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
    UiInteractionTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ui_interaction_tests.moc"
