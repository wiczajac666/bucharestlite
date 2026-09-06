#include "app/theme_manager.hpp"

namespace bl::ui {

QString themeStylesheet(Theme theme) {
    switch (theme) {
    case Theme::Dark:
        return QStringLiteral(
            "QMainWindow, QWidget { background: #232629; color: #e8e8e8; }"
            "QMenuBar, QToolBar, QMenu, QStatusBar { background: #1e2022; color: #e8e8e8; }"
            "QMenuBar::item:selected, QMenu::item:selected { background: #3daee9; color: #000000; }"
            "QMenu::item:disabled { color: #6f7378; }"
            "QToolBar { border: none; }"
            "QDockWidget { font-size: 11pt; }"
            "QDockWidget::title { background: #1e2022; color: #e8e8e8; padding: 3px 6px; }"
            "QPushButton { background: #35383c; color: #e8e8e8; border: 1px solid #464c50;"
            "              padding: 2px 10px; border-radius: 2px; }"
            "QPushButton:hover { background: #45494e; }"
            "QPushButton:disabled { color: #6f7378; }"
            "QLineEdit, QComboBox, QSpinBox { background: #303336; color: #e8e8e8;"
            "              border: 1px solid #464c50; padding: 1px 4px; }"
            "QStatusBar::item { border: none; }"
            "QToolTip { background: #303336; color: #e8e8e8; border: 1px solid #464c50; }");
    case Theme::Light:
        return QStringLiteral(
            "QMainWindow, QWidget { background: #f6f7f8; color: #1c1c1c; }"
            "QMenuBar, QToolBar, QMenu, QStatusBar { background: #eceef0; color: #1c1c1c; }"
            "QMenuBar::item:selected, QMenu::item:selected { background: #b6d7f8; color: #000000; }"
            "QMenu::item:disabled { color: #9a9ea3; }"
            "QToolBar { border: none; }"
            "QDockWidget::title { background: #eceef0; color: #1c1c1c; padding: 3px 6px; }"
            "QPushButton { background: #ffffff; color: #1c1c1c; border: 1px solid #c3c8cc;"
            "              padding: 2px 10px; border-radius: 2px; }"
            "QPushButton:hover { background: #e8ecef; }"
            "QPushButton:disabled { color: #9a9ea3; }"
            "QLineEdit, QComboBox, QSpinBox { background: #ffffff; color: #1c1c1c;"
            "              border: 1px solid #c3c8cc; padding: 1px 4px; }"
            "QStatusBar::item { border: none; }"
            "QToolTip { background: #ffffff; color: #1c1c1c; border: 1px solid #c3c8cc; }");
    }
    return QString();
}

QString themeDisplayName(Theme theme) {
    return theme == Theme::Dark ? QStringLiteral("Dark") : QStringLiteral("Light");
}

QString themeKey(Theme theme) {
    return theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

} // namespace bl::ui