#include "dialogs/duration_dialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bl::ui {

namespace {

int64_t framesOf(bl::Duration d, bl::Rational fps) {
    if (d.ticks < 0) return 0;
    return d.toFramesAt(fps);
}

bl::Duration durationFromFrames(int64_t frames, bl::Rational fps) {
    if (frames < 0) frames = 0;
    return bl::Duration::fromFrames(frames, fps);
}

} // namespace

DurationDialog::DurationDialog(bl::Duration initial, bl::Rational fps,
                               QWidget* parent)
    : QDialog(parent), fps_(fps), duration_(initial) {
    setWindowTitle(tr("Set Duration"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(tr("Duration (HH:MM:SS:FF):"), this);
    layout->addWidget(label);

    edit_ = new QLineEdit(format(initial, fps_), this);
    edit_->setObjectName(QStringLiteral("durationEdit"));
    edit_->selectAll();
    layout->addWidget(edit_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("durationError"));
    errorLabel_->setStyleSheet(QStringLiteral("color: #c05050;"));
    errorLabel_->setVisible(false);
    layout->addWidget(errorLabel_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this,
            &DurationDialog::onAccept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void DurationDialog::onAccept() {
    bl::Duration d{};
    if (!parse(edit_->text(), fps_, d)) {
        updateError(tr("Enter a valid timecode (HH:MM:SS:FF) or frame count."));
        return;
    }
    if (d.ticks <= 0) {
        updateError(tr("Duration must be greater than zero."));
        return;
    }
    duration_ = d;
    accept();
}

void DurationDialog::updateError(const QString& msg) {
    errorLabel_->setText(msg);
    errorLabel_->setVisible(!msg.isEmpty());
}

QString DurationDialog::format(bl::Duration d, bl::Rational fps) {
    const int64_t totalFrames = framesOf(d, fps);
    const int fpsInt = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(fps.num) /
                                        static_cast<double>(fps.den))));
    const int64_t totalSeconds = totalFrames / fpsInt;
    const int ff = static_cast<int>(totalFrames % fpsInt);
    const int ss = static_cast<int>(totalSeconds % 60);
    const int mm = static_cast<int>((totalSeconds / 60) % 60);
    const int hh = static_cast<int>(totalSeconds / 3600);
    return QString::asprintf("%02d:%02d:%02d:%02d", hh, mm, ss, ff);
}

bool DurationDialog::parse(const QString& text, bl::Rational fps,
                           bl::Duration& out) {
    const QString t = text.trimmed();
    // Accept plain frame count.
    bool okFrames = false;
    const qlonglong plain = t.toLongLong(&okFrames);
    if (okFrames) {
        if (plain < 0) return false;
        out = durationFromFrames(static_cast<int64_t>(plain), fps);
        return true;
    }

    static const QRegularExpression re(
        QStringLiteral(R"(^(\d+):(\d{1,2}):(\d{1,2})[:;](\d{1,2})$)"));
    const auto m = re.match(t);
    if (!m.hasMatch()) return false;

    const int fpsInt = std::max(
        1, static_cast<int>(std::lround(static_cast<double>(fps.num) /
                                        static_cast<double>(fps.den))));
    const int hh = m.captured(1).toInt();
    const int mm = m.captured(2).toInt();
    const int ss = m.captured(3).toInt();
    const int ff = m.captured(4).toInt();
    if (mm < 0 || mm >= 60 || ss < 0 || ss >= 60 || ff < 0 || ff >= fpsInt) {
        return false;
    }
    const int64_t totalFrames =
        (((static_cast<int64_t>(hh) * 60 + mm) * 60 + ss) * fpsInt) + ff;
    out = durationFromFrames(totalFrames, fps);
    return true;
}

} // namespace bl::ui