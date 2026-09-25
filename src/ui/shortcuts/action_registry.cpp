#include "shortcuts/action_registry.hpp"

#include <QAction>
#include <QSettings>

namespace bl::ui {

void ActionRegistry::registerAction(QAction* action, const QString& id,
                                    const QString& label,
                                    const QKeySequence& defaultSequence) {
    Entry entry;
    entry.action = action;
    entry.label = label;
    entry.defaultSequence = defaultSequence;
    entry.overrideSequence = QKeySequence();
    entries_[id] = std::move(entry);
    if (action) {
        action->setShortcut(defaultSequence);
    }
}

void ActionRegistry::setOverride(const QString& id,
                                 const QKeySequence& sequence) {
    const auto it = entries_.find(id);
    if (it == entries_.end()) {
        return;
    }
    Entry& entry = it->second;
    entry.overrideSequence = sequence.isEmpty() ? QKeySequence() : sequence;
    const QKeySequence applied =
        entry.overrideSequence.isEmpty() ? entry.defaultSequence
                                         : entry.overrideSequence;
    if (entry.action) {
        entry.action->setShortcut(applied);
    }
}

void ActionRegistry::reset(const QString& id) {
    setOverride(id, QKeySequence());
}

void ActionRegistry::resetAll() {
    for (const auto& [id, entry] : entries_) {
        (void)entry;
        setOverride(id, QKeySequence());
    }
}

QKeySequence ActionRegistry::sequence(const QString& id) const {
    const auto it = entries_.find(id);
    if (it == entries_.end()) {
        return QKeySequence();
    }
    const Entry& entry = it->second;
    const QKeySequence override = entry.overrideSequence;
    return override.isEmpty() ? entry.defaultSequence : override;
}

QAction* ActionRegistry::action(const QString& id) const {
    const auto it = entries_.find(id);
    return it == entries_.end() ? nullptr : it->second.action;
}

bool ActionRegistry::contains(const QString& id) const {
    return entries_.count(id) != 0;
}

std::vector<ActionRegistry::Shortcut> ActionRegistry::shortcuts() const {
    std::vector<Shortcut> result;
    result.reserve(entries_.size());
    for (const auto& [id, entry] : entries_) {
        Shortcut shortcut;
        shortcut.id = id;
        shortcut.label = entry.label;
        shortcut.defaultSequence = entry.defaultSequence;
        result.push_back(shortcut);
    }
    return result;
}

void ActionRegistry::saveOverrides(QSettings& settings,
                                   const QString& groupKey) const {
    settings.beginGroup(groupKey);
    for (const auto& [id, entry] : entries_) {
        const QKeySequence current =
            entry.action ? entry.action->shortcut() : entry.defaultSequence;
        settings.setValue(
            id, current.toString(QKeySequence::PortableText));
    }
    settings.endGroup();
}

int ActionRegistry::loadOverrides(QSettings& settings,
                                  const QString& groupKey) {
    int applied = 0;
    settings.beginGroup(groupKey);
    const QStringList keys = settings.childKeys();
    settings.endGroup();

    for (const QString& id : keys) {
        const auto it = entries_.find(id);
        if (it == entries_.end()) {
            continue;
        }
        settings.beginGroup(groupKey);
        const QKeySequence stored = QKeySequence::fromString(
            settings.value(id).toString(), QKeySequence::PortableText);
        settings.endGroup();
        setOverride(id, stored);
        ++applied;
    }
    return applied;
}

} // namespace bl::ui