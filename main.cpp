#include "mainwindow.h"
#include <QApplication>
#include <QQuickWindow>
#include <QFontDatabase>

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QApplication a(argc, argv);
    a.setApplicationName("GM40 Remote");
    a.setOrganizationName("HughesKettner");
    a.setApplicationVersion(APP_VERSION);

    // Chemins exacts détectés dans ressources.qrc
    QFontDatabase::addApplicationFont(":/fonts/DSEG7Classic-BoldItalic.ttf");
    QFontDatabase::addApplicationFont(":/fonts/DSEG14Classic-BoldItalic.ttf");

    MainWindow w;
    w.show();
    return a.exec();
}
