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
