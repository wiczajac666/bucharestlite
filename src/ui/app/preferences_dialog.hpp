#pragma once

#include <bl_core/proxy.hpp>

#include <QDialog>
#include <QSettings>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QPushButton;
class QFileDialog;

namespace bl::ui {

// Qt-free proxy settings that can be persisted and unit-tested without Qt.
struct ProxySettings {
    std::string proxyDir;          // Empty = "<project_dir>/proxy/"
    uint32_t autoProxyMinWidth = 3840;
    float resolutionFactor = 0.5f;
    int crf = 18;
    std::string codec = "h264";
    unsigned maxConcurrentJobs = 2;
    bool enabled = true;

    // Convert to core ProxyConfig
    bl::ProxyConfig toCoreConfig() const;
    // Load from core ProxyConfig
    void fromCoreConfig(const bl::ProxyConfig& config);
};

// Preferences dialog for proxy generation settings.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QSettings& settings, QWidget* parent = nullptr);
    ~PreferencesDialog() override;

private slots:
    void onBrowseProxyDir();
    void onApply();
    void onResetToDefaults();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void applyToCore();

    QSettings& settings_;
    QLineEdit* proxyDirEdit_{nullptr};
    QSpinBox* autoProxyMinWidthSpin_{nullptr};
    QDoubleSpinBox* resolutionFactorSpin_{nullptr};
    QSpinBox* crfSpin_{nullptr};
    QComboBox* codecCombo_{nullptr};
    QSpinBox* maxConcurrentJobsSpin_{nullptr};
    QCheckBox* enabledCheck_{nullptr};
    QPushButton* browseButton_{nullptr};
    QPushButton* applyButton_{nullptr};
    QPushButton* resetButton_{nullptr};
    QPushButton* okButton_{nullptr};
    QPushButton* cancelButton_{nullptr};
};

} // namespace bl::ui