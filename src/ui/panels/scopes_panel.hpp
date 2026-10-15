#pragma once

#include <bl_render/compositor.hpp>
#include <bl_render/scopes.hpp>

#include <QImage>
#include <QWidget>

class QComboBox;

namespace bl::ui {

// Which video scope is shown.
enum class ScopeMode { Histogram, Waveform, Vectorscope };

// Paints one video scope (histogram / luma waveform / chroma vectorscope) from
// the latest bl::VideoScopes data. The drawing is exposed as renderImage() so
// tests can assert on it without a shown window.
class ScopeView : public QWidget {
    Q_OBJECT
public:
    explicit ScopeView(QWidget* parent = nullptr);

    void setMode(ScopeMode mode);
    ScopeMode mode() const { return mode_; }

    void setScopes(bl::VideoScopes scopes);
    void clear();
    bool hasData() const { return hasData_; }

    // Draws the current mode at `width` x `height` (used by paintEvent, and
    // directly by tests). Never null.
    QImage renderImage(int width, int height) const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ScopeMode mode_{ScopeMode::Histogram};
    bl::VideoScopes scopes_;
    bool hasData_{false};
};

// Scopes dock: a mode picker over a ScopeView. Taps the composited preview
// frame (see PreviewPanel::frameRendered).
class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScopesPanel(QWidget* parent = nullptr);

    ScopeView* view() { return view_; }
    ScopeMode mode() const;

    // Sampling stride used for a frame of this size (>= 1); larger frames are
    // subsampled so a 1080p frame costs about the same as a small one.
    static int sampleStep(int width, int height);

public slots:
    // Recomputes the scopes from a composited frame. An empty/invalid frame
    // clears the view.
    void setFrame(const bl::CompositorResult& frame);

private:
    void applyMode();

    QComboBox* modeCombo_{nullptr};
    ScopeView* view_{nullptr};
};

} // namespace bl::ui
