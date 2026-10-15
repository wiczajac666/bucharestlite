#include "dialogs/quality_metrics_dialog.hpp"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>

#include <gtest/gtest.h>

namespace {

using bl::ui::QualityMetricsDialog;

QApplication* ensureApp() {
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static int argc = 1;
        static char argv0[] = "bl_ui_tests";
        static char* argv[] = {argv0, nullptr};
        return new QApplication(argc, argv);
    }();
    return app;
}

}  // namespace

TEST(QualityMetricsDialogTest, CompareRequiresBothPaths) {
    ensureApp();
    QualityMetricsDialog dialog;

    auto* compare = dialog.findChild<QPushButton*>("qualityCompareButton");
    ASSERT_NE(compare, nullptr);
    EXPECT_FALSE(compare->isEnabled());

    dialog.setReferencePath("/tmp/a.mp4");
    EXPECT_FALSE(compare->isEnabled());
    dialog.setTestPath("/tmp/b.mp4");
    EXPECT_TRUE(compare->isEnabled());
}

TEST(QualityMetricsDialogTest, EmitsCompareRequestWithInputs) {
    ensureApp();
    QualityMetricsDialog dialog;
    dialog.setReferencePath("ref.mp4");
    dialog.setTestPath("test.mp4");
    dialog.setMaxFrames(50);

    QSignalSpy spy(&dialog, &QualityMetricsDialog::compareRequested);
    ASSERT_TRUE(spy.isValid());

    dialog.compareButton()->click();
    ASSERT_EQ(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toString(), QStringLiteral("ref.mp4"));
    EXPECT_EQ(args.at(1).toString(), QStringLiteral("test.mp4"));
    EXPECT_EQ(args.at(2).toInt(), 50);
}

TEST(QualityMetricsDialogTest, ShowReportFormatsMetrics) {
    ensureApp();
    QualityMetricsDialog dialog;

    bl::QualityReport report;
    report.width = 1920;
    report.height = 1080;
    report.framesCompared = 120;
    report.averagePsnr = 42.5;
    report.minPsnr = 40.1;
    report.maxPsnr = 44.0;
    report.averageSsim = 0.9871;
    report.minSsim = 0.9802;
    report.maxSsim = 0.9910;

    dialog.showReport(report);

    auto* results = dialog.findChild<QPlainTextEdit*>("qualityResults");
    ASSERT_NE(results, nullptr);
    const QString text = results->toPlainText();
    EXPECT_TRUE(text.contains(QStringLiteral("Frames compared: 120")));
    EXPECT_TRUE(text.contains(QStringLiteral("1920x1080")));
    EXPECT_TRUE(text.contains(QStringLiteral("PSNR")));
    EXPECT_TRUE(text.contains(QStringLiteral("SSIM")));
    EXPECT_TRUE(text.contains(QStringLiteral("42.50")));
}

TEST(QualityMetricsDialogTest, ShowErrorSurfacesMessage) {
    ensureApp();
    QualityMetricsDialog dialog;
    dialog.showError(QStringLiteral("no video stream"));

    auto* results = dialog.findChild<QPlainTextEdit*>("qualityResults");
    ASSERT_NE(results, nullptr);
    EXPECT_TRUE(results->toPlainText().contains(QStringLiteral("no video stream")));
}
