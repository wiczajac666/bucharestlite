#pragma once

#include <bl_core/command.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bl {

class UndoStack {
public:
    struct State {
        size_t index;
        size_t count;
    };

    struct HistoryEntry {
        size_t index;              // Index in the stack (0-based)
        std::string name;          // Command name
        size_t bytes;              // Memory footprint
        bool isCurrent;            // True if this is the current state (index)
        bool isMacro;              // True if this is a macro/composite command
        std::vector<std::string> macroChildren; // Names of child commands if macro
    };

    UndoStack() = default;
    ~UndoStack() = default;

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    bool push(std::unique_ptr<ICommand> command);

    bool undo();
    bool redo();

    bool canUndo() const;
    bool canRedo() const;

    std::string undoText() const;
    std::string redoText() const;

    size_t count() const;
    size_t index() const;
    State state() const;
    void clear();

    void beginMacro(std::string name);
    bool endMacro();
    bool isMacroActive() const;

    size_t bytesUsed() const;
    size_t memoryLimit() const;
    void setMemoryLimit(size_t limitBytes);

    void setStateCallback(std::function<void()> callback);

    // History panel support
    std::vector<HistoryEntry> getHistory() const;
    // Jump to a specific index in the history (0 = empty, count() = latest)
    bool jumpToIndex(size_t targetIndex);

private:
    struct CompositeCommand;

    void evictWhileOverLimit(std::vector<std::unique_ptr<ICommand>>& entries,
                             size_t& index, size_t& bytesUsed);

    mutable std::recursive_mutex mutex_;
    std::vector<std::unique_ptr<ICommand>> done_;
    size_t index_{0};
    size_t bytesUsed_{0};
    size_t memoryLimit_{SIZE_MAX};

    bool macroActive_{false};
    std::string macroName_;
    std::thread::id macroThread_;
    std::vector<std::unique_ptr<ICommand>> macroChildren_;
    size_t macroBytes_{0};

    std::function<void()> stateCallback_;
};

} // namespace bl
