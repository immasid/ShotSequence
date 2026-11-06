#include <QApplication>
#include <QStyleFactory>
#include "launcher_window.h"

int main(int argc, char *argv[]) {
    qputenv("QT_LOGGING_RULES", "qt.qpa.wayland.textinput.warning=false");

    QApplication app(argc, argv);
    app.setApplicationName("PhotoSort");
    if (QStyle *fusion = QStyleFactory::create("Fusion")) {
        app.setStyle(fusion);
    }

    LauncherWindow window;
    window.show();
    return app.exec();
}
