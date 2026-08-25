#include <bl_core/undo_stack.hpp>

#include <algorithm>
#include <cassert>

namespace bl {

struct UndoStack::CompositeCommand final : public CommandBase {
    std::vector<std::unique_ptr<ICommand>> children;
    size_t childrenBytes{0};

    explicit CompositeCommand(std::string name)
        : CommandBase(std::move(name)) {}

    void redo() override {
        for (auto& child : children) child->redo();
    }

    void undo() override {
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            (*it)->undo();
        }
    }

    size_t bytes() const override {
        return CommandBase::bytes() + sizeof(*this) + childrenBytes +
               children.capacity() * sizeof(std::unique_ptr<ICommand>);
    }
};

void UndoStack::evictWhileOverLimit(
    std::vector<std::unique_ptr<ICommand>>& entries, size_t& index,
    size_t& bytesUsed) {
    while (bytesUsed > memoryLimit_ && entries.size() > 1) {
        bytesUsed -= entries.front()->bytes();
        entries.erase(entries.begin());
        if (index > 0) --index;
    }
}

bool UndoStack::push(std::unique_ptr<ICommand> command) {
    if (!command) return false;

    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (macroActive_) {
            command->redo();
            macroBytes_ += command->bytes();
            macroChildren_.push_back(std::move(command));
            changed = true;
        } else {
            command->redo();

            if (index_ > 0 && done_[index_ - 1]->mergeWith(*command)) {
                const size_t oldBytes = done_[index_ - 1]->bytes();
                bytesUsed_ = bytesUsed_ - oldBytes + done_[index_ - 1]->bytes();
            } else {
                while (done_.size() > index_) {
                    bytesUsed_ -= done_.back()->bytes();
                    done_.pop_back();
                }
                bytesUsed_ += command->bytes();
                done_.push_back(std::move(command));
                index_ = done_.size();
                evictWhileOverLimit(done_, index_, bytesUsed_);
            }
            changed = true;
        }
    }
    if (changed && stateCallback_) stateCallback_();
    return true;
}

bool UndoStack::undo() {
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (macroActive_ || index_ == 0) return false;
        done_[index_ - 1]->undo();
        --index_;
        changed = true;
    }
    if (changed && stateCallback_) stateCallback_();
    return true;
}

bool UndoStack::redo() {
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (macroActive_ || index_ >= done_.size()) return false;
        done_[index_]->redo();
        ++index_;
        changed = true;
    }
    if (changed && stateCallback_) stateCallback_();
    return true;
}

bool UndoStack::canUndo() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !macroActive_ && index_ > 0;
}

bool UndoStack::canRedo() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !macroActive_ && index_ < done_.size();
}

std::string UndoStack::undoText() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (macroActive_ || index_ == 0) return {};
    return done_[index_ - 1]->name();
}

std::string UndoStack::redoText() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (macroActive_ || index_ >= done_.size()) return {};
    return done_[index_]->name();
}

size_t UndoStack::count() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return done_.size();
}

size_t UndoStack::index() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return index_;
}

UndoStack::State UndoStack::state() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return State{index_, done_.size()};
}

void UndoStack::clear() {
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (done_.empty() && macroChildren_.empty() && !macroActive_) {
            return;
        }
        done_.clear();
        macroChildren_.clear();
        macroActive_ = false;
        macroName_.clear();
        macroBytes_ = 0;
        index_ = 0;
        bytesUsed_ = 0;
        changed = true;
    }
    if (changed && stateCallback_) stateCallback_();
}

void UndoStack::beginMacro(std::string name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    assert(!macroActive_ && "nested macros are not supported");
    if (macroActive_) return;
    macroActive_ = true;
    macroName_ = std::move(name);
    macroThread_ = std::this_thread::get_id();
    macroChildren_.clear();
    macroBytes_ = 0;
}

bool UndoStack::endMacro() {
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!macroActive_) return false;
        assert(macroThread_ == std::this_thread::get_id() &&
               "beginMacro/endMacro must pair on the same thread");
        if (macroThread_ != std::this_thread::get_id()) return false;

        macroActive_ = false;
        if (macroChildren_.empty()) {
            macroName_.clear();
            return true;
        }

        auto composite = std::make_unique<CompositeCommand>(macroName_);
        composite->children = std::move(macroChildren_);
        composite->childrenBytes = macroBytes_;

        while (done_.size() > index_) {
            bytesUsed_ -= done_.back()->bytes();
            done_.pop_back();
        }
        bytesUsed_ += composite->bytes();
        done_.push_back(std::move(composite));
        index_ = done_.size();

        macroName_.clear();
        macroChildren_.clear();
        macroBytes_ = 0;

        evictWhileOverLimit(done_, index_, bytesUsed_);
        changed = true;
    }
    if (changed && stateCallback_) stateCallback_();
    return true;
}

bool UndoStack::isMacroActive() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return macroActive_;
}

size_t UndoStack::bytesUsed() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return bytesUsed_;
}

size_t UndoStack::memoryLimit() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return memoryLimit_;
}

void UndoStack::setMemoryLimit(size_t limitBytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    memoryLimit_ = limitBytes;
    evictWhileOverLimit(done_, index_, bytesUsed_);
}

void UndoStack::setStateCallback(std::function<void()> callback) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stateCallback_ = std::move(callback);
}

} // namespace bl
