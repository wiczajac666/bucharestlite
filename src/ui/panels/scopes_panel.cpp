#include "panels/scopes_panel.hpp"

#include <QColor>
#include <QComboBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bl::ui {
namespace {

const QColor kBackdrop{16, 16, 18};
const QColor kGraticule{60, 62, 68};
const QColor kAxis{120, 124, 132};

// `t` in [0,1] -> a visible intensity, lifted so sparse traces stay readable.
int intensity(uint32_t count, uint32_t max) {
    if (count == 0 || max == 0) return 0;
    const double t = std::pow(static_cast<double>(count) / max, 0.4);
    return std::clamp(static_cast<int>(std::lround(t * 255.0)), 0, 255);
}

void paintGraticule(QPainter& painter, int w, int h, bool vertical) {
    painter.setPen(kGraticule);
    if (vertical) {
        for (int i = 1; i < 4; ++i) {
            painter.drawLine(w * i / 4, 0, w * i / 4, h);
        }
    }
    for (int i = 1; i < 4; ++i) {
        painter.drawLine(0, h * i / 4, w, h * i / 4);
    }
    painter.setPen(kAxis);
    painter.drawRect(0, 0, w - 1, h - 1);
}

QImage renderHistogram(const bl::VideoScopes& s, bool hasData, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(kBackdrop);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    paintGraticule(painter, w, h, /*vertical=*/true);

    if (hasData && s.histogramMax > 0) {
        const uint32_t max = s.histogramMax;
        const auto curve = [&](const std::array<uint32_t, 256>& bins,
                               QColor color) {
            QPainterPath path;
            for (int i = 0; i < 256; ++i) {
                const double fx = i / 255.0 * (w - 1);
                const double t = std::pow(static_cast<double>(bins[i]) / max, 0.5);
                const double fy = (h - 1) - t * (h - 1);
                if (i == 0) {
                    path.moveTo(fx, fy);
                } else {
                    path.lineTo(fx, fy);
                }
            }
            painter.setPen(QPen(color, 1.4));
            painter.drawPath(path);
        };
        curve(s.red, QColor(255, 80, 80, 200));
        curve(s.green, QColor(80, 255, 80, 200));
        curve(s.blue, QColor(80, 130, 255, 200));
        curve(s.luma, QColor(240, 240, 240, 110));
    }
    painter.end();
    return img;
}

QImage renderWaveform(const bl::VideoScopes& s, bool hasData, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(kBackdrop);

    QPainter painter(&img);
    if (hasData && s.waveformColumns > 0 && !s.waveform.empty()) {
        // Build a `columns` x 256 intensity map, then scale it to the widget.
        QImage grid(s.waveformColumns, 256, QImage::Format_RGB32);
        for (int col = 0; col < s.waveformColumns; ++col) {
            for (int luma = 0; luma < 256; ++luma) {
                const int v =
                    intensity(s.waveform[static_cast<size_t>(col) * 256 + luma],
                              s.waveformMax);
                // Row 0 of the widget is high luma.
                grid.setPixel(col, 255 - luma, qRgb(v, v, v));
            }
        }
        painter.drawImage(QRect(0, 0, w, h), grid);
        paintGraticule(painter, w, h, /*vertical=*/false);
    }
    painter.end();
    return img;
}

QImage renderVectorscope(const bl::VideoScopes& s, bool hasData, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(kBackdrop);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    const int side = std::min(w, h);
    const QRect target((w - side) / 2, (h - side) / 2, side, side);

    if (hasData && s.vectorscopeSize > 0 && !s.vectorscope.empty()) {
        const int size = s.vectorscopeSize;
        QImage grid(size, size, QImage::Format_RGB32);
        for (int gy = 0; gy < size; ++gy) {
            for (int gx = 0; gx < size; ++gx) {
                const int v =
                    intensity(s.vectorscope[static_cast<size_t>(gy) * size + gx],
                              s.vectorscopeMax);
                // Widget row 0 is high Cr, so flip the chroma row.
                grid.setPixel(gx, size - 1 - gy, qRgb(v, v, v));
            }
        }
        painter.drawImage(target, grid);
    }

    // Circular graticule: outer target ring + a quarter-radius ring + axes.
    painter.setPen(kGraticule);
    painter.drawEllipse(target.adjusted(2, 2, -2, -2));
    painter.drawEllipse(target.center(), side / 4, side / 4);
    painter.setPen(kAxis);
    painter.drawLine(target.center().x(), target.top(), target.center().x(),
                     target.bottom());
    painter.drawLine(target.left(), target.center().y(), target.right(),
                     target.center().y());
    painter.end();
    return img;
}

} // namespace

ScopeView::ScopeView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 120);
}

void ScopeView::setMode(ScopeMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    update();
}

void ScopeView::setScopes(bl::VideoScopes scopes) {
    scopes_ = std::move(scopes);
    hasData_ = true;
    update();
}

void ScopeView::clear() {
    scopes_ = bl::VideoScopes{};
    hasData_ = false;
    update();
}

QImage ScopeView::renderImage(int width, int height) const {
    const int w = std::max(1, width);
    const int h = std::max(1, height);
    switch (mode_) {
        case ScopeMode::Waveform:
            return renderWaveform(scopes_, hasData_, w, h);
        case ScopeMode::Vectorscope:
            return renderVectorscope(scopes_, hasData_, w, h);
        case ScopeMode::Histogram:
        default:
            return renderHistogram(scopes_, hasData_, w, h);
    }
}

QSize ScopeView::sizeHint() const { return QSize(300, 200); }

void ScopeView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawImage(rect(), renderImage(width(), height()));
}

ScopesPanel::ScopesPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    modeCombo_ = new QComboBox(this);
    modeCombo_->setObjectName(QStringLiteral("scopeMode"));
    modeCombo_->addItem(tr("Histogram"));
    modeCombo_->addItem(tr("Waveform"));
    modeCombo_->addItem(tr("Vectorscope"));
    layout->addWidget(modeCombo_, 0);

    view_ = new ScopeView(this);
    view_->setObjectName(QStringLiteral("scopeView"));
    layout->addWidget(view_, 1);

    connect(modeCombo_, &QComboBox::currentIndexChanged, this,
            [this](int) { applyMode(); });
    applyMode();
}

ScopeMode ScopesPanel::mode() const {
    const int index = modeCombo_ ? modeCombo_->currentIndex() : 0;
    if (index <= 0) return ScopeMode::Histogram;
    return static_cast<ScopeMode>(index);
}

void ScopesPanel::applyMode() {
    if (view_) view_->setMode(mode());
}

int ScopesPanel::sampleStep(int width, int height) {
    const double pixels = static_cast<double>(std::max(1, width)) *
                          std::max(1, height);
    constexpr double kTargetSamples = 300000.0;
    if (pixels <= kTargetSamples) return 1;
    return std::max(
        1, static_cast<int>(std::ceil(std::sqrt(pixels / kTargetSamples))));
}

void ScopesPanel::setFrame(const bl::CompositorResult& frame) {
    if (!view_) return;
    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);
    if (w <= 0 || h <= 0 || frame.data.empty()) {
        view_->clear();
        return;
    }
    const int linesize =
        frame.linesize ? static_cast<int>(frame.linesize) : w * 4;
    view_->setScopes(bl::computeScopes(frame.data.data(), w, h, linesize, 256,
                                       256, sampleStep(w, h)));
}

} // namespace bl::ui
