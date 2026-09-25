#pragma once

#include <QKeySequence>
#include <QString>

#include <map>
#include <vector>

class QAction;
class QSettings;

namespace bl::ui {

// Central table of the application's actions and their keyboard shortcuts.
// Every QAction is registered under a stable id with a user-facing label and a
// default sequence; per-id overrides can be set at runtime and persisted to /
// restored from a QSettings group, so the whole binding set is remappable from
// one place (the Keyboard Shortcuts dialog).
class ActionRegistry {
public:
    struct Shortcut {
        QString id;
        QString label;
        QKeySequence defaultSequence;
    };

    void registerAction(QAction* action, const QString& id,
                        const QString& label,
                        const QKeySequence& defaultSequence);

    // Overrides the binding for `id`; an empty sequence restores the default.
    void setOverride(const QString& id, const QKeySequence& sequence);
    void reset(const QString& id);
    void resetAll();

    QKeySequence sequence(const QString& id) const;
    QAction* action(const QString& id) const;
    bool contains(const QString& id) const;

    std::vector<Shortcut> shortcuts() const;

    // Persists every current binding (portable text) into `settings` under
    // `groupKey` and applies serialized overrides back onto the actions.
    void saveOverrides(QSettings& settings, const QString& groupKey) const;
    int loadOverrides(QSettings& settings, const QString& groupKey);

private:
    struct Entry {
        QAction* action{nullptr};
        QString label;
        QKeySequence defaultSequence;
        QKeySequence overrideSequence;
    };

    std::map<QString, Entry> entries_;
};

} // namespace bl::ui