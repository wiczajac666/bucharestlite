#pragma once

#include <bl_render/quality_metrics.hpp>

#include <QDialog>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace bl::ui {

// Modal PSNR/SSIM comparison tool: pick a reference and a test file, run the
// comparison and print the reported metrics. The dialog itself is UI-only; the
// decode work is done by the receiver of compareRequested() (MainWindow), which
// owns a codec registry.
class QualityMetricsDialog : public QDialog {
    Q_OBJECT
public:
    explicit QualityMetricsDialog(QWidget* parent = nullptr);

    QString referencePath() const;
    QString testPath() const;
    int maxFrames() const;  // 0 = compare every frame

    void setReferencePath(const QString& path);
    void setTestPath(const QString& path);
    void setMaxFrames(int frames);

    // Fills the results area with a formatted report.
    void showReport(const bl::QualityReport& report);
    // Shows a failure message in the results area.
    void showError(const QString& message);

    // Test seam: the widget that emits compareRequested().
    QPushButton* compareButton() const { return compareButton_; }

signals:
    // The user asked to compare the two paths. `maxFrames` 0 means all frames.
    void compareRequested(const QString& reference, const QString& test,
                          int maxFrames);

private:
    void browseInto(QLineEdit* target, const QString& title);
    void updateEnabled();

    QLineEdit* referenceEdit_{nullptr};
    QLineEdit* testEdit_{nullptr};
    QSpinBox* maxFramesSpin_{nullptr};
    QPlainTextEdit* results_{nullptr};
    QPushButton* compareButton_{nullptr};
};

}  // namespace bl::ui
