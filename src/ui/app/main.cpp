#include "app/main_window.hpp"

#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("bl_lite"));
    QApplication::setOrganizationName(QStringLiteral("BucharestLite"));

    bl::ui::MainWindow window;
    window.show();
    return app.exec();
}