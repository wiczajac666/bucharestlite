#pragma once

#include <bl_timeline/project_template.hpp>

#include <QDialog>
#include <QString>

#include <string>
#include <vector>

class QListWidget;
class QWidget;

namespace bl::ui {

// Discovers the selectable templates: the compiled-in built-ins followed by
// any user templates under <userDataRoot>/templates (*.blproj / *.json). User
// files are parsed with the project repository and reduced to their sequence
// settings + track skeleton (media and clips are dropped). Unreadable or
// unparsable files are skipped. Discovered user templates are also registered
// with bl::registerUserTemplate so ProjectController::newProjectFromTemplate
// can resolve them by id.
std::vector<ProjectTemplate> discoverTemplates();

// Modal "New from Template…" picker. Lists each template's name and
// description; on accept selectedTemplateId() names the choice.
class TemplatePickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplatePickerDialog(
        QWidget* parent = nullptr,
        std::vector<ProjectTemplate> templates = builtinTemplates());

    const std::vector<ProjectTemplate>& templates() const { return templates_; }

    // Id of the highlighted template, or "" when the list is empty.
    std::string selectedTemplateId() const;

    // Selects the row for `id` (a no-op when it is not listed). Lets tests and
    // callers pre-select a template.
    void selectTemplate(const std::string& id);

private:
    QListWidget* list_{nullptr};
    std::vector<ProjectTemplate> templates_;
};

} // namespace bl::ui
