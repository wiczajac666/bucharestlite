#pragma once

#include <bl_timeline/title.hpp>

#include <QImage>
#include <QString>

namespace bl::ui {

// Rasterises `spec` into a `width` x `height` transparent still (format
// ARGB32_Premultiplied). Text is drawn with the spec's font/colour/outline; an
// optional background rectangle is filled behind the text block. The spec's
// pixel metrics (font size, padding, outline) are interpreted against a 1080p
// reference height and scaled to `height`, so the same spec is correct at any
// resolution. Returns a null image only when width/height are non-positive.
QImage renderTitleImage(const TitleSpec& spec, int width, int height);

// Renders and writes `path` as a PNG. Returns false when the image could not
// be saved (e.g. the directory is missing or unwritable).
bool saveTitlePng(const TitleSpec& spec, int width, int height,
                  const QString& path);

// Directory under the user data root where generated title PNGs are stored
// (created on demand).
QString titleOutputDirectory();

// A fresh, unique PNG path inside titleOutputDirectory() for a new title.
QString newTitleFilePath();

} // namespace bl::ui
