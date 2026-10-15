#include "dialogs/template_picker_dialog.hpp"

#include <bl_core/platform/paths.hpp>
#include <bl_core/project_repository.hpp>

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <string>
#include <utility>

namespace bl::ui {

namespace {

// Reduces a loaded project document to a template: the same sequence settings
// and track skeleton, but no media or clips.
ProjectTemplate templateFromProject(const std::string& id,
                                    const std::string& fallbackName,
                                    const ProjectData& data,
                                    const Timeline& timeline) {
    ProjectTemplate tmpl;
    tmpl.id = id;
    tmpl.name = data.settings.name.empty() ? fallbackName : data.settings.name;
    tmpl.description = QStringLiteral("User template").toStdString();
    tmpl.settings = timeline.sequence().settings;
    tmpl.tracks.reserve(timeline.sequence().videoTracks.size() +
                        timeline.sequence().audioTracks.size());
    for (size_t i = 0; i < timeline.sequence().videoTracks.size(); ++i) {
        tmpl.tracks.push_back(TrackKind::Video);
    }
    for (size_t i = 0; i < timeline.sequence().audioTracks.size(); ++i) {
        tmpl.tracks.push_back(TrackKind::Audio);
    }
    return tmpl;
}

// Best-effort scan of the user templates directory; appends what it can read.
void loadUserTemplates(std::vector<ProjectTemplate>& out) {
    const QString dir = QString::fromStdString(bl::platform::userDataRoot()) +
                        QStringLiteral("/templates");
    QDir qdir(dir);
    if (!qdir.exists()) {
        return;
    }

    const QStringList filters = {QStringLiteral("*.blproj"),
                                 QStringLiteral("*.json")};
    const QFileInfoList entries =
        qdir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& info : entries) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;  // unreadable file: skip it
        }
        const QByteArray bytes = file.readAll();

        auto report = ProjectRepository::fromDocument(
            std::string(bytes.constData(), static_cast<size_t>(bytes.size())),
            info.absoluteFilePath().toStdString());
        if (!report.ok()) {
            continue;  // not a project document
        }

        const nlohmann::json& extensions = report->project.extensions;
        const auto it = extensions.find("timeline");
        if (it == extensions.end() || !it->is_object()) {
            continue;  // no timeline: nothing to make a template from
        }
        Timeline timeline;
        try {
            timeline = it->get<Timeline>();
        } catch (const std::exception&) {
            continue;
        }

        const std::string base = info.completeBaseName().toStdString();
        ProjectTemplate tmpl = templateFromProject(
            "user:" + base, base, report->project, timeline);
        registerUserTemplate(tmpl);
        out.push_back(std::move(tmpl));
    }
}

} // namespace

std::vector<ProjectTemplate> discoverTemplates() {
    clearUserTemplates();
    std::vector<ProjectTemplate> templates = builtinTemplates();
    loadUserTemplates(templates);
    return templates;
}

TemplatePickerDialog::TemplatePickerDialog(
    QWidget* parent, std::vector<ProjectTemplate> templates)
    : QDialog(parent), templates_(std::move(templates)) {
    setWindowTitle(tr("New from Template"));

    auto* layout = new QVBoxLayout(this);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("templatePickerList"));
    for (const ProjectTemplate& tmpl : templates_) {
        QString label = QString::fromStdString(tmpl.name);
        if (!tmpl.description.empty()) {
            label += QStringLiteral(" \u2014 ") +
                     QString::fromStdString(tmpl.description);
        }
        auto* item = new QListWidgetItem(label, list_);
        item->setData(Qt::UserRole, QString::fromStdString(tmpl.id));
        item->setToolTip(QString::fromStdString(tmpl.description));
    }
    if (list_->count() > 0) {
        list_->setCurrentRow(0);
    }
    layout->addWidget(list_);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             this);
    buttons->button(QDialogButtonBox::Ok)
        ->setObjectName(QStringLiteral("templatePickerOk"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
}

std::string TemplatePickerDialog::selectedTemplateId() const {
    const QListWidgetItem* item = list_ ? list_->currentItem() : nullptr;
    if (!item) {
        return std::string();
    }
    return item->data(Qt::UserRole).toString().toStdString();
}

void TemplatePickerDialog::selectTemplate(const std::string& id) {
    if (!list_) {
        return;
    }
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
            list_->setCurrentRow(i);
            return;
        }
    }
}

} // namespace bl::ui
