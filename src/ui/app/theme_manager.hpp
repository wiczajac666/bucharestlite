#pragma once

#include <QString>

namespace bl::ui {

enum class Theme { Dark, Light };

QString themeStylesheet(Theme theme);
QString themeDisplayName(Theme theme);
QString themeKey(Theme theme);

} // namespace bl::ui