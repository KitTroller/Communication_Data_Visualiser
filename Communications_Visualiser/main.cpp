#include <QApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    // Force Qt to bypass macOS native styling BEFORE the app engine boots
    // This entirely replaces the need for #include <QQuickStyle>
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Communications_Visualiser", "Main");

    return app.exec();
}