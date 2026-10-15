#include "title/title_renderer.hpp"

#include <bl_core/platform/paths.hpp>

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace bl::ui {

namespace {

// Reference canvas height the spec's pixel metrics are authored against.
constexpr double kReferenceHeight = 1080.0;

QColor toColor(const TitleColor& c) {
    return QColor(c.r, c.g, c.b, c.a);
}

} // namespace

QImage renderTitleImage(const TitleSpec& specIn, int width, int height) {
    if (width <= 0 || height <= 0) return QImage();

    const TitleSpec spec = clampTitleSpec(specIn);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const double scale = static_cast<double>(height) / kReferenceHeight;

    QFont font(QString::fromStdString(spec.fontFamily));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(spec.fontSizePx * scale))));
    font.setBold(spec.bold);
    font.setItalic(spec.italic);

    const QFontMetricsF fm(font);
    const QStringList lines =
        QString::fromStdString(spec.text).split(QLatin1Char('\n'));

    const double step = fm.height() * spec.lineSpacing;
    const double blockH =
        fm.height() + (lines.isEmpty() ? 0.0 : step * (lines.size() - 1));

    double blockW = 0.0;
    for (const QString& line : lines) {
        blockW = std::max(blockW, fm.horizontalAdvance(line));
    }

    // Anchor the block: `align` picks which horizontal point of the block sits
    // at anchorX; vertically the block centre sits at anchorY.
    const double anchorPxX = spec.anchorX * width;
    const double anchorPxY = spec.anchorY * height;
    double blockX = anchorPxX;
    if (spec.align == TitleAlign::Center) blockX -= blockW / 2.0;
    else if (spec.align == TitleAlign::Right) blockX -= blockW;
    const double blockY = anchorPxY - blockH / 2.0;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const double pad = spec.paddingPx * scale;
    if (spec.backgroundEnabled) {
        const QRectF rect(blockX - pad, blockY - pad, blockW + 2.0 * pad,
                          blockH + 2.0 * pad);
        painter.fillRect(rect, toColor(spec.backgroundColor));
    }

    const double outline = spec.outlinePx * scale;
    const QColor textColor = toColor(spec.textColor);
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.isEmpty()) continue;
        const double lineW = fm.horizontalAdvance(line);
        double lineX = blockX;
        if (spec.align == TitleAlign::Center) lineX += (blockW - lineW) / 2.0;
        else if (spec.align == TitleAlign::Right) lineX += (blockW - lineW);
        const double baselineY = blockY + fm.ascent() + step * i;

        QPainterPath path;
        path.addText(lineX, baselineY, font, line);
        if (outline > 0.0) {
            QPen pen(toColor(spec.outlineColor), outline * 2.0, Qt::SolidLine,
                     Qt::RoundCap, Qt::RoundJoin);
            painter.strokePath(path, pen);
        }
        painter.fillPath(path, textColor);
    }

    painter.end();
    return image;
}

bool saveTitlePng(const TitleSpec& spec, int width, int height,
                  const QString& path) {
    const QImage image = renderTitleImage(spec, width, height);
    if (image.isNull()) return false;
    return image.save(path, "PNG");
}

QString titleOutputDirectory() {
    const QString root =
        QString::fromStdString(bl::platform::userDataRoot());
    const QString dir = root + QStringLiteral("/titles");
    QDir().mkpath(dir);
    return dir;
}

QString newTitleFilePath() {
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_hhmmsszzz"));
    return titleOutputDirectory() +
           QStringLiteral("/title_%1.png").arg(stamp);
}

} // namespace bl::ui
