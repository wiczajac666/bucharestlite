#pragma once

#include <QElapsedTimer>
#include <QWidget>

namespace bl::ui {

// Pair of vertical peak-dB bars for mixer metering. Inputs are linear
// amplitudes (like the ones TrackStrip applies to decoded PCM); levels are
// displayed on a -60..+12 dB scale with green (quiet), amber (hot) and red
// (clipping) zones plus a slowly-releasing peak-hold cap line. Ballistics and
// the peak hold are paint-driven, so setLevels() is safe to call at any rate.
//
// level(channel) returns the last-set linear input and is the deterministic
// hook UI tests assert against (painting itself is cosmetic).
class LevelMeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeterWidget(QWidget* parent = nullptr);

    void setLevels(float left, float right);

    float level(int channel) const noexcept;
    // Linear amplitude -> dB on the meter's scale floor (-60).
    static float levelToDb(float value);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float levels_[2]{0.0f, 0.0f};
    float display_[2]{0.0f, 0.0f};
    float hold_[2]{0.0f, 0.0f};
    QElapsedTimer clock_;
    qint64 lastPaintMs_{-1};
};

} // namespace bl::ui