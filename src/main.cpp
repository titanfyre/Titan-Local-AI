#include "AppController.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("titanfyre");
    QCoreApplication::setApplicationName("TitanLocalAI");
    QCoreApplication::setApplicationVersion(TITAN_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Titan Local AI — assistant local C++");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Render the UI with isolated test settings and exit."});
    parser.addOption({"screenshot", "Capture the rendered UI and exit.", "path"});
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    if (smoke)
        QStandardPaths::setTestModeEnabled(true);
    QQuickStyle::setStyle("Basic");
    app.setWindowIcon(QIcon(":/assets/app-icon.ico"));
    titan::AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("app", &controller);
    int qmlErrors = 0;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError> &errors) {
        for (const auto &e : errors) {
            std::fprintf(stderr, "%s\n", qPrintable(e.toString()));
            if (e.toString().contains("ReferenceError") || e.toString().contains("TypeError") ||
                e.toString().contains("is not defined"))
                ++qmlErrors;
        }
    });
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); },
        Qt::QueuedConnection);
    engine.loadFromModule("TitanLocalAI", "Main");
    if (engine.rootObjects().isEmpty())
        return 2;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window) {
        if (auto *screen = window->screen()) {
            auto rect = screen->availableGeometry();
            window->resize(qMin(window->width(), qMax(960, rect.width() - 60)),
                           qMin(window->height(), qMax(660, rect.height() - 80)));
        }
#ifdef Q_OS_WIN
        auto updateCaption = [&] {
            BOOL dark = controller.settings().value("theme").toString() != "light";
            DwmSetWindowAttribute(reinterpret_cast<HWND>(window->winId()), 20, &dark, sizeof(dark));
        };
        updateCaption();
        QObject::connect(&controller, &titan::AppController::settingsChanged, window, updateCaption);
#endif
    }
    if (smoke || parser.isSet("screenshot"))
        QTimer::singleShot(1800, &app, [&] {
            if (parser.isSet("screenshot") && window) {
                const auto path = parser.value("screenshot");
                QDir().mkpath(QFileInfo(path).absolutePath());
                if (!window->grabWindow().save(path)) {
                    std::fprintf(stderr, "UI screenshot failed\n");
                    app.exit(3);
                    return;
                }
            }
            app.exit(qmlErrors ? 4 : 0);
        });
    return app.exec();
}
