#include "dialogs/export_dialog.hpp"

#include <QCheckBox>
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
    auto* outputLayout = new QGridLayout(outputGroup);
    outputLayout->addWidget(new QLabel(tr("Target:"), outputGroup), 0, 0);
    targetCombo_ = new QComboBox(outputGroup);
    targetCombo_->setObjectName(QStringLiteral("exportTarget"));
    targetCombo_->addItem(tr("Save to File"),
                          static_cast<int>(ExportTarget::File));
    targetCombo_->addItem(tr("Stream to IP"),
                          static_cast<int>(ExportTarget::Stream));
    outputLayout->addWidget(targetCombo_, 0, 1, 1, 2);

    outputPathEdit_ = new QLineEdit(outputGroup);
    outputPathEdit_->setObjectName(QStringLiteral("exportOutputPath"));
    outputPathEdit_->setPlaceholderText(
        tr("Select destination file (e.g. my-movie.mp4)..."));
    browseButton_ = new QPushButton(tr("Browse..."), outputGroup);
    browseButton_->setObjectName(QStringLiteral("exportBrowse"));
    outputLayout->addWidget(outputPathEdit_, 1, 0, 1, 2);
    outputLayout->addWidget(browseButton_, 1, 2);

    streamUrlEdit_ = new QLineEdit(outputGroup);
    streamUrlEdit_->setObjectName(QStringLiteral("exportStreamUrl"));
    streamUrlEdit_->setPlaceholderText(
        tr("rtmp://host/app/key  or  udp://host:port"));
    outputLayout->addWidget(streamUrlEdit_, 2, 0, 1, 3);
    root->addWidget(outputGroup);

    // Preset ------------------------------------------------------------
    auto* presetGroup = new QGroupBox(tr("Preset"), this);
    auto* presetLayout = new QHBoxLayout(presetGroup);
    presetCombo_ = new QComboBox(presetGroup);
    presetCombo_->setObjectName(QStringLiteral("exportPreset"));
    presetCombo_->addItem(tr("Custom"), QString());
    for (const ExportPreset& p : exportPresets()) {
        presetCombo_->addItem(QString::fromStdString(p.name),
                              QString::fromStdString(p.id));
    }
    presetLayout->addWidget(presetCombo_, 1);
    root->addWidget(presetGroup);

    // Format ------------------------------------------------------------
    auto* formatGroup = new QGroupBox(tr("Format"), this);
    auto* formatLayout = new QGridLayout(formatGroup);
    formatLayout->addWidget(new QLabel(tr("Container:"), formatGroup), 0, 0);
    containerCombo_ = new QComboBox(formatGroup);
    containerCombo_->setObjectName(QStringLiteral("exportContainer"));
    containerCombo_->addItem("MP4 (.mp4)", QStringLiteral("mp4"));
    containerCombo_->addItem("WebM (.webm)", QStringLiteral("webm"));
    containerCombo_->addItem("Matroska (.mkv)", QStringLiteral("mkv"));
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

    auto* audioGroup = new QHBoxLayout();
    includeAudioCheck_ = new QCheckBox(tr("Include audio"), formatGroup);
    includeAudioCheck_->setObjectName(QStringLiteral("exportIncludeAudio"));
    includeAudioCheck_->setChecked(true);
    audioCodecCombo_ = new QComboBox(formatGroup);
    audioCodecCombo_->setObjectName(QStringLiteral("exportAudioCodec"));
    audioGroup->addWidget(includeAudioCheck_);
    audioGroup->addWidget(audioCodecCombo_, 1);
    formatLayout->addLayout(audioGroup, 6, 0, 1, 2);

    formatLayout->addWidget(new QLabel(tr("Range:"), formatGroup), 7, 0);
    rangeCombo_ = new QComboBox(formatGroup);
    rangeCombo_->setObjectName(QStringLiteral("exportRange"));
    rangeCombo_->addItem(tr("Entire project"),
                         static_cast<int>(ExportRange::EntireProject));
    rangeCombo_->addItem(tr("Timeline zone"),
                         static_cast<int>(ExportRange::InOut));
    rangeCombo_->addItem(tr("Selected clip"),
                         static_cast<int>(ExportRange::Clip));
    // Zone/clip ranges only become pickable once the caller supplies them.
    if (auto* rangeModel =
            qobject_cast<QStandardItemModel*>(rangeCombo_->model())) {
        if (auto* zoneItem = rangeModel->item(1)) zoneItem->setEnabled(false);
        if (auto* clipItem = rangeModel->item(2)) clipItem->setEnabled(false);
    }
    formatLayout->addWidget(rangeCombo_, 7, 1);
    root->addWidget(formatGroup);

    // Buttons -----------------------------------------------------------
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    exportButton_ = buttons->button(QDialogButtonBox::Save);
    exportButton_->setObjectName(QStringLiteral("exportStart"));
    cancelButton_ = buttons->button(QDialogButtonBox::Cancel);
    cancelButton_->setObjectName(QStringLiteral("exportDialogCancel"));
    root->addWidget(buttons);

    connect(browseButton_, &QPushButton::clicked, this,
            &ExportDialog::browseOutput);
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { applyPreset(index); });
    connect(targetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateTargetRows();
                updateWidgets();
            });
    connect(outputPathEdit_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateWidgets(); });
    connect(streamUrlEdit_, &QLineEdit::textChanged, this,
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
                } else if (container == QLatin1String("mkv")) {
                    // MKV accepts any video codec; no forced switching needed.
                }
                rebuildAudioCodecItems();
                updateWidgets();
            });
    connect(includeAudioCheck_, &QCheckBox::toggled, this, [this](bool) {
        rebuildAudioCodecItems();
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
    updateTargetRows();
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

void ExportDialog::rebuildAudioCodecItems() {
    if (!audioCodecCombo_) return;
    const QString previous = audioCodecCombo_->currentData().toString();
    const QString container = containerCombo_->currentData().toString();
    const bool streaming =
        targetCombo_ &&
        targetCombo_->currentData().toInt() ==
            static_cast<int>(ExportTarget::Stream);
    const bool enabled = includeAudioCheck_->isChecked() && !streaming;

    audioCodecCombo_->blockSignals(true);
    audioCodecCombo_->clear();
    if (streaming) {
        // FLV and MPEG-TS publish H.264 + AAC; AAC is the only audio codec.
        audioCodecCombo_->addItem(tr("AAC"), QStringLiteral("aac"));
        audioCodecCombo_->setCurrentIndex(0);
    } else {
        if (container == QLatin1String("webm")) {
            audioCodecCombo_->addItem(tr("Opus"), QStringLiteral("opus"));
            audioCodecCombo_->addItem(tr("Vorbis"), QStringLiteral("vorbis"));
        } else if (container == QLatin1String("mkv")) {
            audioCodecCombo_->addItem(tr("AAC"), QStringLiteral("aac"));
            audioCodecCombo_->addItem(tr("FLAC"), QStringLiteral("flac"));
            audioCodecCombo_->addItem(tr("Opus"), QStringLiteral("opus"));
            audioCodecCombo_->addItem(tr("Vorbis"), QStringLiteral("vorbis"));
        } else {
            audioCodecCombo_->addItem(tr("AAC"), QStringLiteral("aac"));
            audioCodecCombo_->addItem(tr("FLAC"), QStringLiteral("flac"));
        }
        const int idx = audioCodecCombo_->findData(previous);
        audioCodecCombo_->setCurrentIndex(
            idx >= 0 ? idx
                     : audioCodecCombo_->findData(
                           container == QLatin1String("webm")
                               ? QStringLiteral("opus")
                               : container == QLatin1String("mkv")
                                     ? QStringLiteral("aac")
                                     : QStringLiteral("aac")));
    }
    audioCodecCombo_->setEnabled(enabled);
    audioCodecCombo_->blockSignals(false);
}

void ExportDialog::updateTargetRows() {
    const bool streaming =
        targetCombo_ &&
        targetCombo_->currentData().toInt() ==
            static_cast<int>(ExportTarget::Stream);

    // File path row is only meaningful for a file target; the stream URL row
    // only for a network target. Both swaps keep the encode fields coherent:
    // streaming infers the container and forces H.264 + AAC.
    if (outputPathEdit_) outputPathEdit_->setEnabled(!streaming);
    if (browseButton_) browseButton_->setEnabled(!streaming);
    if (streamUrlEdit_) {
        streamUrlEdit_->setVisible(streaming);
        streamUrlEdit_->setEnabled(streaming);
    }
    if (containerCombo_) containerCombo_->setEnabled(!streaming);
    if (videoCodecCombo_) {
        if (streaming) {
            const int h264 = videoCodecCombo_->findData(QStringLiteral("h264"));
            if (h264 >= 0) videoCodecCombo_->setCurrentIndex(h264);
        }
        videoCodecCombo_->setEnabled(!streaming);
    }
    rebuildAudioCodecItems();
}

void ExportDialog::updateWidgets() {
    const bool custom =
        scaleCombo_->currentData().toInt() ==
        static_cast<int>(ResolutionScale::Custom);
    customWidthSpin_->setEnabled(custom);
    customHeightSpin_->setEnabled(custom);

    const ExportSettings s = settings();
    if (audioCodecCombo_) {
        // The stream audio codec is force-set to AAC, so the combo stays
        // disabled while streaming even when audio is included.
        audioCodecCombo_->setEnabled(includeAudioCheck_->isChecked() &&
                                     s.target != ExportTarget::Stream);
    }
    const uint32_t w = outputWidth(s, sequence_);
    const uint32_t h = outputHeight(s, sequence_);
    if (resolutionLabel_) {
        resolutionLabel_->setText(tr("Result: %1x%2").arg(w).arg(h));
    }
    if (exportButton_) {
        exportButton_->setEnabled(s.target == ExportTarget::Stream
                                      ? !s.streamUrl.empty()
                                      : !s.outputPath.empty());
    }
}

ExportSettings ExportDialog::settings() const {
    ExportSettings s;
    s.target = targetCombo_
                   ? static_cast<ExportTarget>(targetCombo_->currentData().toInt())
                   : ExportTarget::File;
    s.includeAudio =
        includeAudioCheck_ ? includeAudioCheck_->isChecked() : false;
    s.videoCq = cqSpin_->value();
    s.scale =
        static_cast<ResolutionScale>(scaleCombo_->currentData().toInt());
    s.customWidth = static_cast<uint32_t>(customWidthSpin_->value());
    s.customHeight = static_cast<uint32_t>(customHeightSpin_->value());
    s.range = static_cast<ExportRange>(rangeCombo_->currentData().toInt());
    if (s.range == ExportRange::InOut && hasZone_) {
        s.inPoint = zoneIn_;
        s.outPoint = zoneOut_;
    } else if (s.range == ExportRange::Clip && hasClipRange_) {
        s.inPoint = clipIn_;
        s.outPoint = clipOut_;
    } else {
        s.range = ExportRange::EntireProject;
    }

    if (s.target == ExportTarget::Stream) {
        // The stream URL is the target; container and codecs are inferred and
        // constrained (the plan validates the URL scheme).
        s.streamUrl =
            streamUrlEdit_ ? streamUrlEdit_->text().toStdString() : std::string();
        s.videoCodec = "h264";
        s.audioCodec = s.includeAudio ? std::string("aac") : std::string();
        return s;
    }

    s.outputPath = outputPathEdit_->text().toStdString();
    s.container = containerCombo_->currentData().toString().toStdString();
    s.videoCodec = videoCodecCombo_->currentData().toString().toStdString();
    s.audioCodec =
        (s.includeAudio && audioCodecCombo_)
            ? audioCodecCombo_->currentData().toString().toStdString()
            : std::string();
    return s;
}

void ExportDialog::setZone(bool hasZone, Time in, Time out) {
    hasZone_ = hasZone;
    zoneIn_ = in;
    zoneOut_ = out;
    if (rangeCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(rangeCombo_->model());
        if (model && model->item(1)) model->item(1)->setEnabled(hasZone);
        if (!hasZone && rangeCombo_->currentIndex() == 1) {
            rangeCombo_->setCurrentIndex(0);
        }
    }
    updateWidgets();
}

void ExportDialog::setClipRange(bool hasClip, Time in, Time out) {
    hasClipRange_ = hasClip;
    clipIn_ = in;
    clipOut_ = out;
    if (rangeCombo_) {
        auto* model = qobject_cast<QStandardItemModel*>(rangeCombo_->model());
        if (model && model->item(2)) model->item(2)->setEnabled(hasClip);
        if (!hasClip && rangeCombo_->currentIndex() == 2) {
            rangeCombo_->setCurrentIndex(0);
        }
    }
    updateWidgets();
}

void ExportDialog::applyPreset(int index) {
    if (!presetCombo_) return;
    const QString id = presetCombo_->itemData(index).toString();
    if (id.isEmpty()) return; // "Custom": leave the fields as they are.
    const ExportPreset* preset = findExportPreset(id.toStdString());
    if (!preset) return;
    const ExportSettings& s = preset->settings;

    const auto selectData = [](QComboBox* box, const QVariant& presetValue) {
        if (!box) return;
        const int idx = box->findData(presetValue);
        if (idx >= 0) box->setCurrentIndex(idx);
    };
    // Container first: its handler reconciles the codec/audio lists.
    selectData(containerCombo_, QString::fromStdString(s.container));
    selectData(videoCodecCombo_, QString::fromStdString(s.videoCodec));
    selectData(scaleCombo_, QVariant(static_cast<int>(s.scale)));
    cqSpin_->setValue(s.videoCq);
    customWidthSpin_->setValue(static_cast<int>(s.customWidth));
    customHeightSpin_->setValue(static_cast<int>(s.customHeight));
    includeAudioCheck_->setChecked(s.includeAudio);
    // Reconciles the audio list (and re-forces H.264/AAC while streaming).
    updateTargetRows();
    if (s.includeAudio) {
        selectData(audioCodecCombo_, QString::fromStdString(s.audioCodec));
    }
    updateWidgets();
}

} // namespace bl::ui