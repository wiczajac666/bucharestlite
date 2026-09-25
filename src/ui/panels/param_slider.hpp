#pragma once

#include <bl_render/effect.hpp>

#include <QWidget>

class QLabel;
class QSlider;

namespace bl::ui {

// A single named parameter editor: label, integer-scaled range slider and a
// live value readout. Emission of valueCommitted() happens once per drag
// gesture (on slider release), so callers push a single undoable command.
class ParamSlider : public QWidget {
    Q_OBJECT
public:
    explicit ParamSlider(const bl::ParamSpec& spec, QWidget* parent = nullptr);

    const std::string& key() const { return spec_.key; }
    double value() const { return value_; }

    // Updates the control without emitting valueCommitted().
    void setValue(double value);

    // Release gesture: emits valueCommitted() with the current value. The UI
    // path fires on slider release; tests call this directly.
    void commit();

signals:
    void valueCommitted(double value);

private:
    void onSliderMoved(int position);
    void updateReadout();

    bl::ParamSpec spec_;
    double value_{0.0};
    QSlider* slider_{nullptr};
    QLabel* valueLabel_{nullptr};
};

} // namespace bl::ui