#include "dialogs/export_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace bl::ui {

namespace {

constexpr int kCqMaximum = 51;

} // namespace

ExportDialog::ExportDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("ExportDialog"));
    setWindowTitle(tr("Export"));
    setModal(true);
    resize(460, 360);

    auto* root = new QVBoxLayout(this);

    // Output target -----------------------------------------------------
    auto* outputGroup = new QGroupBox(tr("Output"), this);
    auto* outputLayout = new QHBoxLayout(outputGroup);
    outputPathEdit_ = new QLineEdit(outputGroup);
    outputPathEdit_->setObjectName(QStringLiteral("exportOutputPath"));
    outputPathEdit_->setPlaceholderText(
        tr("Select destination file (e.g. my-movie.mp4)..."));
    auto* browseButton = new QPushButton(tr("Browse..."), outputGroup);
    browseButton->setObjectName(QStringLiteral("exportBrowse"));
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton);
    root->addWidget(outputGroup);

    // Format ------------------------------------------------------------
    auto* formatGroup = new QGroupBox(tr("Format"), this);
    auto* formatLayout = new QGridLayout(formatGroup);
    formatLayout->addWidget(new QLabel(tr("Container:"), formatGroup), 0, 0);
    containerCombo_ = new QComboBox(formatGroup);
    containerCombo_->setObjectName(QStringLiteral("exportContainer"));
    containerCombo_->addItem("MP4 (.mp4)", QStringLiteral("mp4"));
    containerCombo_->addItem("WebM (.webm)", QStringLiteral("webm"));
    formatLayout->addWidget(containerCombo_, 0, 1);

    formatLayout->addWidget(new QLabel(tr("Video codec:"), formatGroup), 1, 0);
    videoCodecCombo_ = new QComboBox(formatGroup);
    videoCodecCombo_->setObjectName(QStringLiteral("exportVideoCodec"));
    videoCodecCombo_->addItem("H.264", QStringLiteral("h264"));
    videoCodecCombo_->addItem("VP9", QStringLiteral("vp9"));
    videoCodecCombo_->addItem("AV1", QStringLiteral("av1"));
    videoCodecCombo_->addItem("MPEG-4", QStringLiteral("mpeg4"));
    videoCodecCombo_->addItem("Theora", QStringLiteral("theora"));
    formatLayout->addWidget(videoCodecCombo_, 1, 1);

    formatLayout->addWidget(new QLabel(tr("Quality (CRF):"), formatGroup), 2, 0);
    cqSpin_ = new QSpinBox(formatGroup);
    cqSpin_->setObjectName(QStringLiteral("exportCq"));
    cqSpin_->setRange(0, kCqMaximum);
    cqSpin_->setValue(23);
    cqSpin_->setToolTip(tr("Constant-rate factor; 0 uses the encoder default, "
                           "lower is better quality (larger file)"));
    formatLayout->addWidget(cqSpin_, 2, 1);

    formatLayout->addWidget(new QLabel(tr("Resolution:"), formatGroup), 3, 0);
    scaleCombo_ = new QComboBox(formatGroup);
    scaleCombo_->setObjectName(QStringLiteral("exportScale"));
    scaleCombo_->addItem(tr("Full"), static_cast<int>(ResolutionScale::Full));
    scaleCombo_->addItem(tr("Half"), static_cast<int>(ResolutionScale::Half));
    scaleCombo_->addItem(tr("Quarter"),
                         static_cast<int>(ResolutionScale::Quarter));
    scaleCombo_->addItem(tr("Custom"),
                         static_cast<int>(ResolutionScale::Custom));
    formatLayout->addWidget(scaleCombo_, 3, 1);

    resolutionLabel_ = new QLabel(formatGroup);
    formatLayout->addWidget(resolutionLabel_, 4, 0, 1, 2);

    auto* customLayout = new QGridLayout();
    customLayout->addWidget(new QLabel(tr("Width:"), formatGroup), 0, 0);
    customWidthSpin_ = new QSpinBox(formatGroup);
    customWidthSpin_->setObjectName(QStringLiteral("exportCustomWidth"));
    customWidthSpin_->setRange(16, 8192);
    customLayout->addWidget(customWidthSpin_, 0, 1);
    customLayout->addWidget(new QLabel(tr("Height:"), formatGroup), 0, 2);
    customHeightSpin_ = new QSpinBox(formatGroup);
    customHeightSpin_->setObjectName(QStringLiteral("exportCustomHeight"));
    customHeightSpin_->setRange(16, 8192);
    customLayout->addWidget(customHeightSpin_, 0, 3);
    formatLayout->addLayout(customLayout, 5, 0, 1, 2);

    auto* audioNote = new QLabel(
        tr("Audio tracks are not exported in this version."), formatGroup);
    audioNote->setObjectName(QStringLiteral("exportAudioNote"));
    audioNote->setEnabled(false);
    formatLayout->addWidget(audioNote, 6, 0, 1, 2);
    root->addWidget(formatGroup);

    // Buttons -----------------------------------------------------------
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    exportButton_ = buttons->button(QDialogButtonBox::Save);
    exportButton_->setObjectName(QStringLiteral("exportStart"));
    cancelButton_ = buttons->button(QDialogButtonBox::Cancel);
    cancelButton_->setObjectName(QStringLiteral("exportDialogCancel"));
    root->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this,
            &ExportDialog::browseOutput);
    connect(outputPathEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateWidgets(); });
    connect(containerCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                const QString container = containerCombo_->currentData().toString();
                const QString codec = videoCodecCombo_->currentData().toString();
                if (container == QLatin1String("webm") &&
                    codec != QLatin1String("vp9") &&
                    codec != QLatin1String("av1") &&
                    codec != QLatin1String("theora")) {
                    videoCodecCombo_->setCurrentIndex(
                        videoCodecCombo_->findData(QStringLiteral("vp9")));
                } else if (container == QLatin1String("mp4") &&
                           (codec == QLatin1String("vp9") ||
                            codec == QLatin1String("theora"))) {
                    videoCodecCombo_->setCurrentIndex(
                        videoCodecCombo_->findData(QStringLiteral("h264")));
                }
                updateWidgets();
            });
    connect(scaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateWidgets(); });
    connect(customWidthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updateWidgets(); });
    connect(videoCodecCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { updateWidgets(); });
    connect(cqSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { updateWidgets(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);

    setSequenceDefaults(SequenceSettings{});
    updateWidgets();
}

void ExportDialog::setSequenceDefaults(const SequenceSettings& seq) {
    sequence_ = seq;
    customWidthSpin_->setValue(
        seq.width > 0 ? static_cast<int>(seq.width) : 1920);
    customHeightSpin_->setValue(
        seq.height > 0 ? static_cast<int>(seq.height) : 1080);
    updateWidgets();
}

void ExportDialog::browseOutput() {
    const QString extension =
        containerCombo_->currentData().toString().isEmpty()
            ? QStringLiteral("mp4")
            : containerCombo_->currentData().toString();
    const QString filter =
        QStringLiteral("Video (*.%1)").arg(extension);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export video"), QString(), filter);
    if (!path.isEmpty()) {
        QString resolved = path;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix.isEmpty()) {
            resolved += QLatin1Char('.') + extension;
        } else if (suffix != extension &&
                   (suffix == QLatin1String("mp4") ||
                    suffix == QLatin1String("webm"))) {
            resolved =
                resolved.left(resolved.size() - suffix.size()) + extension;
        }
        outputPathEdit_->setText(resolved);
    }
    updateWidgets();
}

void ExportDialog::updateWidgets() {
    const bool custom =
        scaleCombo_->currentData().toInt() ==
        static_cast<int>(ResolutionScale::Custom);
    customWidthSpin_->setEnabled(custom);
    customHeightSpin_->setEnabled(custom);

    const ExportSettings s = settings();
    const uint32_t w = outputWidth(s, sequence_);
    const uint32_t h = outputHeight(s, sequence_);
    if (resolutionLabel_) {
        resolutionLabel_->setText(tr("Result: %1x%2").arg(w).arg(h));
    }
    if (exportButton_) {
        exportButton_->setEnabled(!s.outputPath.empty());
    }
}

ExportSettings ExportDialog::settings() const {
    ExportSettings s;
    s.outputPath = outputPathEdit_->text().toStdString();
    s.container = containerCombo_->currentData().toString().toStdString();
    s.videoCodec = videoCodecCombo_->currentData().toString().toStdString();
    s.includeAudio = false;
    s.audioCodec.clear();
    s.videoCq = cqSpin_->value();
    s.scale =
        static_cast<ResolutionScale>(scaleCombo_->currentData().toInt());
    s.customWidth = static_cast<uint32_t>(customWidthSpin_->value());
    s.customHeight = static_cast<uint32_t>(customHeightSpin_->value());
    s.range = ExportRange::EntireProject;
    return s;
}

} // namespace bl::ui