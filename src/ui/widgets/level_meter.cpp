#include "widgets/level_meter.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace bl::ui {

namespace {

constexpr float kFloorDb{-60.0f};
constexpr float kCeilingDb{12.0f};
constexpr float kScaleDb{kCeilingDb - kFloorDb}; // 72 dB
constexpr int kBarCount{2};
constexpr int kBarMargin{2};
constexpr int kBarSpacing{2};

constexpr int kGreenEndDb{-12};
constexpr int kAmberEndDb{0};

QColor zoneColor(int db) {
    if (db >= kAmberEndDb) return QColor(0xe0, 0x18, 0x2f);  // red (clip zone)
    if (db >= kGreenEndDb) return QColor(0xff, 0xc1, 0x07);  // amber (hot)
    return QColor(0x4c, 0xaf, 0x50);                          // green
}

float zoneTop(int db) {
    const float f = (static_cast<float>(db) - kFloorDb) / kScaleDb;
    return std::clamp(f, 0.0f, 1.0f);
}

} // namespace

LevelMeterWidget::LevelMeterWidget(QWidget* parent) : QWidget(parent) {
    clock_.start();
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void LevelMeterWidget::setLevels(float left, float right) {
    levels_[0] = left;
    levels_[1] = right;
    update();
}

float LevelMeterWidget::level(int channel) const noexcept {
    if (channel < 0 || channel >= kBarCount) return 0.0f;
    return levels_[channel];
}

float LevelMeterWidget::levelToDb(float value) {
    if (value <= 0.0f) return kFloorDb;
    return std::max(kFloorDb, 20.0f * std::log10(value));
}

QSize LevelMeterWidget::minimumSizeHint() const {
    const int barWidth = 10;
    const int width = kBarMargin * 2 + kBarCount * barWidth +
                      (kBarCount - 1) * kBarSpacing;
    return QSize(width, 64);
}

QSize LevelMeterWidget::sizeHint() const {
    return QSize(32, 96);
}

void LevelMeterWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window().color());

    const qint64 nowMs = clock_.elapsed();
    const double elapsedSec =
        lastPaintMs_ > 0
            ? std::max(0.0, static_cast<double>(nowMs - lastPaintMs_) / 1000.0)
            : 0.0;
    lastPaintMs_ = nowMs;

    const int barWidth = 10;
    const int trackTop = 1;
    const int trackBottom = height() - 1;
    const int trackH = trackBottom - trackTop;

    for (int ch = 0; ch < kBarCount; ++ch) {
        const int x = kBarMargin + ch * (barWidth + kBarSpacing);
        const QRect track(x, trackTop, barWidth, trackH);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1));
        painter.setBrush(palette().color(QPalette::Window).darker(115));
        painter.drawRect(track);

        const float targetDb = levelToDb(levels_[ch]);
        float& disp = display_[ch];
        if (targetDb > disp) {
            disp = targetDb;
        } else if (elapsedSec > 0.0) {
            // Release at roughly 20 dB/s so hot peaks taper visibly.
            disp = std::max(targetDb,
                            disp - static_cast<float>(elapsedSec * 20.0));
        }

        float& hold = hold_[ch];
        if (disp > hold) {
            hold = disp;
        } else if (elapsedSec > 0.0) {
            hold = std::max(disp, hold - static_cast<float>(elapsedSec * 3.0));
        }

        const float fillFraction =
            std::clamp((disp - kFloorDb) / kScaleDb, 0.0f, 1.0f);
        if (fillFraction <= 0.0f) {
            continue;
        }

        const QRect fill(track.x() + 1, trackTop + 1, barWidth - 2, trackH - 2);
        const float h = static_cast<float>(fill.height());
        const float greenTop = zoneTop(kGreenEndDb) * h;
        const float amberTop = zoneTop(kAmberEndDb) * h;
        const float fillHeight = fillFraction * h;

        // Draw the contiguous fill as stacked zone rects (green below -12 dB,
        // amber -12..0 dB, red above 0 dB).
        const struct { float top; float bottom; int labelDb; } zones[3] = {
            {0.0f, greenTop, -20},
            {greenTop, amberTop, -6},
            {amberTop, h, 6},
        };
        for (const auto& zone : zones) {
            const float zBottom = std::min(fillHeight, zone.bottom);
            const float zTop = std::min(fillHeight, zone.top);
            if (zBottom <= zTop) continue;
            const QRect zr(fill.x(),
                           fill.y() + fill.height() - static_cast<int>(zBottom),
                           fill.width(), static_cast<int>(zBottom - zTop));
            painter.fillRect(zr, zoneColor(zone.labelDb));
        }

        // Peak-hold cap line.
        const float holdFraction =
            std::clamp((hold - kFloorDb) / kScaleDb, 0.0f, 1.0f);
        if (holdFraction > 0.0f) {
            const int capY = fill.y() + fill.height() -
                             static_cast<int>(holdFraction *
                                              static_cast<float>(fill.height()));
            painter.fillRect(fill.x(), capY, fill.width(), 2,
                             palette().color(QPalette::Text));
        }
    }
}

} // namespace bl::ui