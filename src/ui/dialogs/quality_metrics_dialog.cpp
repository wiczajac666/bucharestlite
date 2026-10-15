#include "dialogs/quality_metrics_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>

namespace bl::ui {

namespace {

constexpr char kMediaFilter[] =
    "Media Files (*.mp4 *.mkv *.webm *.mov *.avi *.m4v);;All Files (*)";

}  // namespace

QualityMetricsDialog::QualityMetricsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Compare Quality (PSNR / SSIM)"));
    setObjectName(QStringLiteral("qualityMetricsDialog"));

    referenceEdit_ = new QLineEdit(this);
    referenceEdit_->setObjectName(QStringLiteral("qualityReferenceEdit"));
    auto* refRow = new QWidget(this);
    auto* refLayout = new QHBoxLayout(refRow);
    refLayout->setContentsMargins(0, 0, 0, 0);
    refLayout->addWidget(referenceEdit_, 1);
    auto* refBrowse = new QPushButton(tr("Browse..."), refRow);
    refBrowse->setObjectName(QStringLiteral("qualityReferenceBrowse"));
    refLayout->addWidget(refBrowse);
    connect(refBrowse, &QPushButton::clicked, this,
            [this] { browseInto(referenceEdit_, tr("Reference File")); });

    testEdit_ = new QLineEdit(this);
    testEdit_->setObjectName(QStringLiteral("qualityTestEdit"));
    auto* testRow = new QWidget(this);
    auto* testLayout = new QHBoxLayout(testRow);
    testLayout->setContentsMargins(0, 0, 0, 0);
    testLayout->addWidget(testEdit_, 1);
    auto* testBrowse = new QPushButton(tr("Browse..."), testRow);
    testBrowse->setObjectName(QStringLiteral("qualityTestBrowse"));
    testLayout->addWidget(testBrowse);
    connect(testBrowse, &QPushButton::clicked, this,
            [this] { browseInto(testEdit_, tr("Test File")); });

    maxFramesSpin_ = new QSpinBox(this);
    maxFramesSpin_->setObjectName(QStringLiteral("qualityMaxFrames"));
    maxFramesSpin_->setRange(0, 1000000);
    maxFramesSpin_->setValue(0);
    maxFramesSpin_->setSpecialValueText(tr("All"));
    maxFramesSpin_->setToolTip(tr("Maximum frames to compare (0 = all)"));

    auto* form = new QFormLayout();
    form->addRow(tr("Reference:"), refRow);
    form->addRow(tr("Test:"), testRow);
    form->addRow(tr("Max frames:"), maxFramesSpin_);

    results_ = new QPlainTextEdit(this);
    results_->setObjectName(QStringLiteral("qualityResults"));
    results_->setReadOnly(true);
    results_->setPlaceholderText(
        tr("Reference is expected to be the higher-quality source."));

    compareButton_ = new QPushButton(tr("Compare"), this);
    compareButton_->setObjectName(QStringLiteral("qualityCompareButton"));
    compareButton_->setDefault(true);
    connect(compareButton_, &QPushButton::clicked, this, [this] {
        emit compareRequested(referenceEdit_->text().trimmed(),
                              testEdit_->text().trimmed(), maxFramesSpin_->value());
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addButton(compareButton_, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Results:"), this));
    layout->addWidget(results_, 1);
    layout->addWidget(buttons);

    connect(referenceEdit_, &QLineEdit::textChanged, this,
            &QualityMetricsDialog::updateEnabled);
    connect(testEdit_, &QLineEdit::textChanged, this,
            &QualityMetricsDialog::updateEnabled);
    updateEnabled();

    resize(560, 420);
}

QString QualityMetricsDialog::referencePath() const {
    return referenceEdit_->text().trimmed();
}

QString QualityMetricsDialog::testPath() const {
    return testEdit_->text().trimmed();
}

int QualityMetricsDialog::maxFrames() const { return maxFramesSpin_->value(); }

void QualityMetricsDialog::setReferencePath(const QString& path) {
    referenceEdit_->setText(path);
}

void QualityMetricsDialog::setTestPath(const QString& path) {
    testEdit_->setText(path);
}

void QualityMetricsDialog::setMaxFrames(int frames) {
    maxFramesSpin_->setValue(frames);
}

void QualityMetricsDialog::showReport(const bl::QualityReport& report) {
    std::ostringstream out;
    out << "Frames compared: " << report.framesCompared;
    if (report.frameCountMismatch) {
        out << "  (frame counts differ)";
    }
    out << "\n";
    out << "Resolution: " << report.width << "x" << report.height << "\n";
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "PSNR  avg " << report.averagePsnr << " dB  (min " << report.minPsnr
        << ", max " << report.maxPsnr << ")\n";
    out.precision(4);
    out << "SSIM  avg " << report.averageSsim << "  (min " << report.minSsim
        << ", max " << report.maxSsim << ")\n";
    results_->setPlainText(QString::fromStdString(out.str()));
}

void QualityMetricsDialog::showError(const QString& message) {
    results_->setPlainText(tr("Comparison failed: %1").arg(message));
}

void QualityMetricsDialog::browseInto(QLineEdit* target, const QString& title) {
    const QString path = QFileDialog::getOpenFileName(
        this, title, target->text(), tr(kMediaFilter));
    if (!path.isEmpty()) {
        target->setText(path);
    }
}

void QualityMetricsDialog::updateEnabled() {
    compareButton_->setEnabled(!referencePath().isEmpty() &&
                               !testPath().isEmpty());
}

}  // namespace bl::ui
