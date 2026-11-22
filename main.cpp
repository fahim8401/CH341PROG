#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // Qt5 modal dialogs (QMessageBox) can be created without keyboard focus
    // under Wayland, so they appear but cannot be dismissed and the main
    // window is blocked meanwhile (looks like a hang). Fall back to XWayland
    // when it is available, unless the platform was chosen explicitly.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    QApplication a(argc, argv);
    QApplication::setOrganizationName("CH341SPI");
    QApplication::setApplicationName("CH341SPI");
    MainWindow w;
    w.show();

    return a.exec();
}
