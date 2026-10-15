#include "dialogs/title_editor_dialog.hpp"

#include "title/title_renderer.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bl::ui {

namespace {

constexpr int kPreviewW = 480;
constexpr int kPreviewH = 270;

QColor toQColor(const TitleColor& c) { return QColor(c.r, c.g, c.b, c.a); }

TitleColor fromQColor(const QColor& c) {
    return TitleColor{static_cast<uint8_t>(c.red()),
                      static_cast<uint8_t>(c.green()),
                      static_cast<uint8_t>(c.blue()),
                      static_cast<uint8_t>(c.alpha())};
}

} // namespace

TitleEditorDialog::TitleEditorDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("TitleEditorDialog"));
    setWindowTitle(tr("Title Editor"));
    buildUi();
    setSpec(defaultTitleSpec());
    setDurationSeconds(5.0);
    updatePreview();
}

void TitleEditorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    root->addLayout(row);

    auto* form = new QFormLayout();
    row->addLayout(form, 1);

    textEdit_ = new QPlainTextEdit(this);
    textEdit_->setObjectName(QStringLiteral("titleText"));
    textEdit_->setMinimumHeight(60);
    form->addRow(tr("Text"), textEdit_);

    fontCombo_ = new QFontComboBox(this);
    fontCombo_->setObjectName(QStringLiteral("titleFont"));
    form->addRow(tr("Font"), fontCombo_);

    sizeSpin_ = new QSpinBox(this);
    sizeSpin_->setObjectName(QStringLiteral("titleFontSize"));
    sizeSpin_->setRange(8, 400);
    form->addRow(tr("Size"), sizeSpin_);

    boldCheck_ = new QCheckBox(tr("Bold"), this);
    boldCheck_->setObjectName(QStringLiteral("titleBold"));
    italicCheck_ = new QCheckBox(tr("Italic"), this);
    italicCheck_->setObjectName(QStringLiteral("titleItalic"));
    {
        auto* styleRow = new QHBoxLayout();
        styleRow->addWidget(boldCheck_);
        styleRow->addWidget(italicCheck_);
        form->addRow(tr("Style"), styleRow);
    }

    textColorButton_ = new QPushButton(tr("Text Colour..."), this);
    textColorButton_->setObjectName(QStringLiteral("titleTextColor"));
    form->addRow(tr("Colour"), textColorButton_);

    alignCombo_ = new QComboBox(this);
    alignCombo_->setObjectName(QStringLiteral("titleAlign"));
    alignCombo_->addItem(tr("Left"));
    alignCombo_->addItem(tr("Center"));
    alignCombo_->addItem(tr("Right"));
    form->addRow(tr("Align"), alignCombo_);

    anchorXSlider_ = new QSlider(Qt::Horizontal, this);
    anchorXSlider_->setObjectName(QStringLiteral("titleAnchorX"));
    anchorXSlider_->setRange(0, 100);
    form->addRow(tr("Position X"), anchorXSlider_);

    anchorYSlider_ = new QSlider(Qt::Horizontal, this);
    anchorYSlider_->setObjectName(QStringLiteral("titleAnchorY"));
    anchorYSlider_->setRange(0, 100);
    form->addRow(tr("Position Y"), anchorYSlider_);

    bgCheck_ = new QCheckBox(tr("Enabled"), this);
    bgCheck_->setObjectName(QStringLiteral("titleBackground"));
    form->addRow(tr("Background"), bgCheck_);

    bgColorButton_ = new QPushButton(tr("Background Colour..."), this);
    bgColorButton_->setObjectName(QStringLiteral("titleBgColor"));
    form->addRow(QString(), bgColorButton_);

    paddingSpin_ = new QSpinBox(this);
    paddingSpin_->setObjectName(QStringLiteral("titlePadding"));
    paddingSpin_->setRange(0, 200);
    form->addRow(tr("Padding"), paddingSpin_);

    outlineSpin_ = new QSpinBox(this);
    outlineSpin_->setObjectName(QStringLiteral("titleOutline"));
    outlineSpin_->setRange(0, 50);
    form->addRow(tr("Outline"), outlineSpin_);

    outlineColorButton_ = new QPushButton(tr("Outline Colour..."), this);
    outlineColorButton_->setObjectName(QStringLiteral("titleOutlineColor"));
    form->addRow(QString(), outlineColorButton_);

    durationSpin_ = new QSpinBox(this);
    durationSpin_->setObjectName(QStringLiteral("titleDuration"));
    durationSpin_->setRange(1, 3600);
    durationRow_ = new QWidget(this);
    auto* durationLayout = new QHBoxLayout(durationRow_);
    durationLayout->setContentsMargins(0, 0, 0, 0);
    durationLayout->addWidget(durationSpin_);
    form->addRow(tr("Duration (s)"), durationRow_);

    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName(QStringLiteral("titlePreview"));
    previewLabel_->setMinimumSize(kPreviewW / 2, kPreviewH / 2);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet(
        QStringLiteral("background:#202020; border:1px solid #444;"));
    row->addWidget(previewLabel_, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto preview = [this] { updatePreview(); };
    connect(textEdit_, &QPlainTextEdit::textChanged, this, preview);
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this, preview);
    connect(sizeSpin_, &QSpinBox::valueChanged, this, preview);
    connect(boldCheck_, &QCheckBox::toggled, this, preview);
    connect(italicCheck_, &QCheckBox::toggled, this, preview);
    connect(alignCombo_, &QComboBox::currentIndexChanged, this, preview);
    connect(anchorXSlider_, &QSlider::valueChanged, this, preview);
    connect(anchorYSlider_, &QSlider::valueChanged, this, preview);
    connect(bgCheck_, &QCheckBox::toggled, this, preview);
    connect(paddingSpin_, &QSpinBox::valueChanged, this, preview);
    connect(outlineSpin_, &QSpinBox::valueChanged, this, preview);

    connect(textColorButton_, &QPushButton::clicked, this,
            [this] { pickColor(textColor_, textColorButton_); });
    connect(bgColorButton_, &QPushButton::clicked, this,
            [this] { pickColor(bgColor_, bgColorButton_); });
    connect(outlineColorButton_, &QPushButton::clicked, this,
            [this] { pickColor(outlineColor_, outlineColorButton_); });
}

void TitleEditorDialog::setCanvasSize(int width, int height) {
    canvasWidth_ = width > 0 ? width : 1920;
    canvasHeight_ = height > 0 ? height : 1080;
    updatePreview();
}

void TitleEditorDialog::setSpec(const TitleSpec& specIn) {
    const TitleSpec spec = clampTitleSpec(specIn);
    textEdit_->setPlainText(QString::fromStdString(spec.text));
    fontCombo_->setCurrentFont(QFont(QString::fromStdString(spec.fontFamily)));
    sizeSpin_->setValue(static_cast<int>(std::lround(spec.fontSizePx)));
    boldCheck_->setChecked(spec.bold);
    italicCheck_->setChecked(spec.italic);
    textColor_ = spec.textColor;
    refreshColorButton(toQColor(textColor_), textColorButton_);
    alignCombo_->setCurrentIndex(static_cast<int>(spec.align));
    anchorXSlider_->setValue(
        static_cast<int>(std::lround(spec.anchorX * 100.0)));
    anchorYSlider_->setValue(
        static_cast<int>(std::lround(spec.anchorY * 100.0)));
    bgCheck_->setChecked(spec.backgroundEnabled);
    bgColor_ = spec.backgroundColor;
    refreshColorButton(toQColor(bgColor_), bgColorButton_);
    paddingSpin_->setValue(static_cast<int>(std::lround(spec.paddingPx)));
    outlineSpin_->setValue(static_cast<int>(std::lround(spec.outlinePx)));
    outlineColor_ = spec.outlineColor;
    refreshColorButton(toQColor(outlineColor_), outlineColorButton_);
    updatePreview();
}

TitleSpec TitleEditorDialog::specFromWidgets() const {
    TitleSpec spec;
    spec.text = textEdit_->toPlainText().toStdString();
    spec.fontFamily = fontCombo_->currentFont().family().toStdString();
    spec.fontSizePx = static_cast<double>(sizeSpin_->value());
    spec.bold = boldCheck_->isChecked();
    spec.italic = italicCheck_->isChecked();
    spec.textColor = textColor_;
    const int alignIndex = alignCombo_->currentIndex();
    spec.align = alignIndex == 0   ? TitleAlign::Left
                 : alignIndex == 2 ? TitleAlign::Right
                                   : TitleAlign::Center;
    spec.anchorX = static_cast<double>(anchorXSlider_->value()) / 100.0;
    spec.anchorY = static_cast<double>(anchorYSlider_->value()) / 100.0;
    spec.backgroundEnabled = bgCheck_->isChecked();
    spec.backgroundColor = bgColor_;
    spec.paddingPx = static_cast<double>(paddingSpin_->value());
    spec.outlinePx = static_cast<double>(outlineSpin_->value());
    spec.outlineColor = outlineColor_;
    return spec;
}

TitleSpec TitleEditorDialog::spec() const {
    return clampTitleSpec(specFromWidgets());
}

void TitleEditorDialog::setDurationSeconds(double seconds) {
    const int clamped = std::min(3600, std::max(1, static_cast<int>(std::lround(seconds))));
    durationSpin_->setValue(clamped);
}

double TitleEditorDialog::durationSeconds() const {
    return static_cast<double>(durationSpin_->value());
}

void TitleEditorDialog::setDurationVisible(bool visible) {
    if (durationRow_) durationRow_->setVisible(visible);
}

QImage TitleEditorDialog::renderAtCanvas() const {
    return renderTitleImage(spec(), canvasWidth_, canvasHeight_);
}

void TitleEditorDialog::updatePreview() {
    const QImage image = renderAtCanvas();
    if (image.isNull()) {
        previewLabel_->clear();
        return;
    }
    previewLabel_->setPixmap(QPixmap::fromImage(image).scaled(
        kPreviewW, kPreviewH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void TitleEditorDialog::pickColor(TitleColor& target, QPushButton* button) {
    const QColor chosen = QColorDialog::getColor(
        toQColor(target), this, tr("Choose Colour"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    target = fromQColor(chosen);
    refreshColorButton(chosen, button);
    updatePreview();
}

void TitleEditorDialog::refreshColorButton(const QColor& color,
                                           QPushButton* button) {
    if (!button) return;
    button->setStyleSheet(QStringLiteral("background-color: %1;")
                              .arg(color.name(QColor::HexRgb)));
    button->setText(color.name(QColor::HexRgb));
}

} // namespace bl::ui
