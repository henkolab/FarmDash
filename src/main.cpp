#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("FarmRun");
    QCoreApplication::setApplicationName("FarmRun");
    MainWindow w;
    w.resize(1280, 800);
    w.show();
    return app.exec();
}
