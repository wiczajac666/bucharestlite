#include "app/preferences_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <bl_core/proxy.hpp>

namespace bl::ui {

bl::ProxyConfig ProxySettings::toCoreConfig() const {
    bl::ProxyConfig config;
    config.proxyDir = proxyDir;
    config.autoProxyMinWidth = autoProxyMinWidth;
    config.resolutionFactor = resolutionFactor;
    config.crf = crf;
    config.codec = codec;
    config.maxConcurrentJobs = maxConcurrentJobs;
    config.enabled = enabled;
    return config;
}

void ProxySettings::fromCoreConfig(const bl::ProxyConfig& config) {
    proxyDir = config.proxyDir;
    autoProxyMinWidth = config.autoProxyMinWidth;
    resolutionFactor = config.resolutionFactor;
    crf = config.crf;
    codec = config.codec;
    maxConcurrentJobs = config.maxConcurrentJobs;
    enabled = config.enabled;
}

PreferencesDialog::PreferencesDialog(QSettings& settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    setWindowTitle(tr("Proxy Preferences"));
    setModal(true);
    resize(480, 400);
    setupUi();
    loadSettings();
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Proxy directory group
    auto* dirGroup = new QGroupBox(tr("Proxy Storage"), this);
    auto* dirLayout = new QFormLayout(dirGroup);
    dirLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    dirLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    proxyDirEdit_ = new QLineEdit(this);
    proxyDirEdit_->setPlaceholderText(tr("Empty = project_dir/proxy/"));
    proxyDirEdit_->setReadOnly(true);

    browseButton_ = new QPushButton(tr("Browse..."), this);
    connect(browseButton_, &QPushButton::clicked, this, &PreferencesDialog::onBrowseProxyDir);

    auto* dirRow = new QHBoxLayout();
    dirRow->addWidget(proxyDirEdit_, 1);
    dirRow->addWidget(browseButton_);
    dirLayout->addRow(tr("Proxy Directory:"), dirRow);
    dirLayout->addRow(new QLabel(tr("Leave empty to store proxies next to the project file.")));

    mainLayout->addWidget(dirGroup);

    // Proxy generation settings group
    auto* genGroup = new QGroupBox(tr("Proxy Generation"), this);
    auto* genLayout = new QFormLayout(genGroup);
    genLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    genLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    enabledCheck_ = new QCheckBox(tr("Enable proxy generation"), this);
    genLayout->addRow(enabledCheck_);

    autoProxyMinWidthSpin_ = new QSpinBox(this);
    autoProxyMinWidthSpin_->setRange(1280, 16000);
    autoProxyMinWidthSpin_->setSingleStep(120);
    autoProxyMinWidthSpin_->setSuffix(" px");
    autoProxyMinWidthSpin_->setToolTip(tr("Auto-generate proxies for media with width >= this value"));
    genLayout->addRow(tr("Auto-generate threshold:"), autoProxyMinWidthSpin_);

    resolutionFactorSpin_ = new QDoubleSpinBox(this);
    resolutionFactorSpin_->setRange(0.1, 1.0);
    resolutionFactorSpin_->setSingleStep(0.125);
    resolutionFactorSpin_->setDecimals(3);
    resolutionFactorSpin_->setToolTip(tr("Proxy resolution as fraction of source (0.5 = half, 0.25 = quarter)"));
    genLayout->addRow(tr("Resolution factor:"), resolutionFactorSpin_);

    crfSpin_ = new QSpinBox(this);
    crfSpin_->setRange(0, 51);
    crfSpin_->setToolTip(tr("Constant Rate Factor (lower = higher quality, larger file). 0 = encoder default."));
    genLayout->addRow(tr("CRF (quality):"), crfSpin_);

    codecCombo_ = new QComboBox(this);
    codecCombo_->addItems({"h264", "hevc", "vp9", "av1"});
    codecCombo_->setToolTip(tr("Video codec for proxy files"));
    genLayout->addRow(tr("Codec:"), codecCombo_);

    maxConcurrentJobsSpin_ = new QSpinBox(this);
    maxConcurrentJobsSpin_->setRange(1, 8);
    maxConcurrentJobsSpin_->setToolTip(tr("Maximum simultaneous proxy generation jobs"));
    genLayout->addRow(tr("Max concurrent jobs:"), maxConcurrentJobsSpin_);

    mainLayout->addWidget(genGroup);

    // Buttons
    auto* buttonBox = new QDialogButtonBox(this);
    applyButton_ = buttonBox->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    resetButton_ = buttonBox->addButton(tr("Reset to Defaults"), QDialogButtonBox::ResetRole);
    okButton_ = buttonBox->addButton(QDialogButtonBox::Ok);
    cancelButton_ = buttonBox->addButton(QDialogButtonBox::Cancel);

    connect(applyButton_, &QPushButton::clicked, this, &PreferencesDialog::onApply);
    connect(resetButton_, &QPushButton::clicked, this, &PreferencesDialog::onResetToDefaults);
    connect(okButton_, &QPushButton::clicked, this, [this] {
        onApply();
        accept();
    });
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

void PreferencesDialog::loadSettings() {
    settings_.beginGroup("proxy");

    proxyDirEdit_->setText(QString::fromStdString(settings_.value("proxyDir", "").toString().toStdString()));
    enabledCheck_->setChecked(settings_.value("enabled", true).toBool());
    autoProxyMinWidthSpin_->setValue(settings_.value("autoProxyMinWidth", 3840).toInt());
    resolutionFactorSpin_->setValue(settings_.value("resolutionFactor", 0.5).toDouble());
    crfSpin_->setValue(settings_.value("crf", 18).toInt());
    codecCombo_->setCurrentText(QString::fromStdString(settings_.value("codec", "h264").toString().toStdString()));
    maxConcurrentJobsSpin_->setValue(settings_.value("maxConcurrentJobs", 2).toInt());

    settings_.endGroup();
}

void PreferencesDialog::saveSettings() {
    settings_.beginGroup("proxy");

    settings_.setValue("proxyDir", proxyDirEdit_->text().toStdString().c_str());
    settings_.setValue("enabled", enabledCheck_->isChecked());
    settings_.setValue("autoProxyMinWidth", autoProxyMinWidthSpin_->value());
    settings_.setValue("resolutionFactor", resolutionFactorSpin_->value());
    settings_.setValue("crf", crfSpin_->value());
    settings_.setValue("codec", codecCombo_->currentText().toStdString().c_str());
    settings_.setValue("maxConcurrentJobs", maxConcurrentJobsSpin_->value());

    settings_.endGroup();
    settings_.sync();
}

void PreferencesDialog::onBrowseProxyDir() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Proxy Directory"),
        proxyDirEdit_->text().isEmpty() ? QDir::homePath() : proxyDirEdit_->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        proxyDirEdit_->setText(dir);
    }
}

void PreferencesDialog::onApply() {
    saveSettings();
    applyToCore();
    QMessageBox::information(this, tr("Proxy Preferences"),
                             tr("Proxy settings applied. Changes will affect future proxy generations."));
}

void PreferencesDialog::onResetToDefaults() {
    proxyDirEdit_->clear();
    enabledCheck_->setChecked(true);
    autoProxyMinWidthSpin_->setValue(3840);
    resolutionFactorSpin_->setValue(0.5);
    crfSpin_->setValue(18);
    codecCombo_->setCurrentText("h264");
    maxConcurrentJobsSpin_->setValue(2);
}

void PreferencesDialog::applyToCore() {
    // The core ProxyGenerator will pick up new settings when next used
    // (it reads from QSettings on construction or via explicit config update)
    // For immediate effect, we could emit a signal, but for now the next
    // proxy generation will pick up the new settings.
}

} // namespace bl::ui