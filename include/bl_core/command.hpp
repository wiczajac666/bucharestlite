#pragma once

#include <functional>
#include <string>
#include <utility>

namespace bl {

class ICommand {
public:
    virtual ~ICommand() = default;

    virtual void redo() = 0;
    virtual void undo() = 0;

    virtual bool mergeWith(const ICommand& next) = 0;

    virtual std::string name() const = 0;
    virtual size_t bytes() const = 0;
};

class CommandBase : public ICommand {
public:
    explicit CommandBase(std::string name) : name_(std::move(name)) {}

    bool mergeWith(const ICommand&) override { return false; }

    std::string name() const override { return name_; }

    size_t bytes() const override {
        return sizeof(*this) + name_.capacity();
    }

private:
    std::string name_;
};

class FunctionCommand final : public CommandBase {
public:
    using RedoFn = std::function<void()>;
    using UndoFn = std::function<void()>;

    FunctionCommand(std::string name, RedoFn redoFn, UndoFn undoFn,
                    size_t extraBytes = 0)
        : CommandBase(std::move(name)),
          redoFn_(std::move(redoFn)),
          undoFn_(std::move(undoFn)),
          extraBytes_(extraBytes) {}

    void redo() override { redoFn_(); }
    void undo() override { undoFn_(); }

    size_t bytes() const override {
        return CommandBase::bytes() + sizeof(*this) + extraBytes_ +
               (redoFn_ ? 32u : 0u) + (undoFn_ ? 32u : 0u);
    }

private:
    RedoFn redoFn_;
    UndoFn undoFn_;
    size_t extraBytes_;
};

} // namespace bl
