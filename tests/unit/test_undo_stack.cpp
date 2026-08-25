#include <bl_core/undo_stack.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using bl::CommandBase;
using bl::FunctionCommand;
using bl::ICommand;
using bl::UndoStack;

class ValueCommand : public CommandBase {
public:
    ValueCommand(std::string name, std::vector<int>& log, int value)
        : CommandBase(std::move(name)), log_(log), value_(value) {}

    void redo() override { log_.push_back(value_); }
    void undo() override { log_.pop_back(); }

    size_t bytes() const override {
        return CommandBase::bytes() + extraBytes_;
    }

    void setExtraBytes(size_t b) { extraBytes_ = b; }

private:
    std::vector<int>& log_;
    int value_;
    size_t extraBytes_{0};
};

TEST(UndoStackTest, PushExecutesAndUndoReverts) {
    UndoStack stack;
    std::vector<int> log;

    EXPECT_TRUE(stack.push(std::make_unique<ValueCommand>("a", log, 1)));
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.back(), 1);
    EXPECT_EQ(stack.count(), 1u);
    EXPECT_EQ(stack.index(), 1u);

    EXPECT_TRUE(stack.undo());
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(stack.index(), 0u);
    EXPECT_EQ(stack.count(), 1u);

    EXPECT_TRUE(stack.redo());
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.back(), 1);
}

TEST(UndoStackTest, CanUndoCanRedoTransitions) {
    UndoStack stack;
    std::vector<int> log;

    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());

    stack.push(std::make_unique<ValueCommand>("a", log, 1));
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());

    stack.undo();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_TRUE(stack.canRedo());

    stack.redo();
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, UndoRedoRoundTripsMultipleSteps) {
    UndoStack stack;
    std::vector<int> log;

    for (int i = 1; i <= 3; ++i) {
        stack.push(std::make_unique<ValueCommand>("step" + std::to_string(i),
                                                  log, i));
    }
    ASSERT_EQ(log.size(), 3u);

    while (stack.canUndo()) stack.undo();
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(stack.index(), 0u);

    while (stack.canRedo()) stack.redo();
    EXPECT_EQ(log, (std::vector<int>{1, 2, 3}));
}

TEST(UndoStackTest, PushAfterUndoTruncatesTail) {
    UndoStack stack;
    std::vector<int> log;

    stack.push(std::make_unique<ValueCommand>("one", log, 1));
    stack.push(std::make_unique<ValueCommand>("two", log, 2));
    stack.push(std::make_unique<ValueCommand>("three", log, 3));
    stack.undo();
    stack.undo();
    ASSERT_EQ(stack.count(), 3u);
    ASSERT_EQ(stack.index(), 1u);

    const size_t bytesBefore = stack.bytesUsed();
    stack.push(std::make_unique<ValueCommand>("replacement", log, 9));

    EXPECT_EQ(stack.count(), 2u);
    EXPECT_EQ(stack.index(), 2u);
    EXPECT_LT(stack.bytesUsed(), bytesBefore + sizeof(ValueCommand) * 2);
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, MergeCollapsesConsecutiveCommands) {
    class MergeableVolume : public CommandBase {
    public:
        MergeableVolume(int& target, int level)
            : CommandBase("volume"), target_(target), prev_(target),
              level_(level) {}

        bool mergeWith(const ICommand& next) override {
            auto other = dynamic_cast<const MergeableVolume*>(&next);
            if (!other) return false;
            level_ = other->level_;
            return true;
        }

        void redo() override { target_ = level_; }
        void undo() override { target_ = prev_; }

    private:
        int& target_;
        int prev_;
        int level_;
    };

    UndoStack stack;
    int volume = 0;

    stack.push(std::make_unique<MergeableVolume>(volume, 10));
    stack.push(std::make_unique<MergeableVolume>(volume, 20));
    stack.push(std::make_unique<MergeableVolume>(volume, 30));

    EXPECT_EQ(volume, 30);
    EXPECT_EQ(stack.count(), 1u);
    EXPECT_EQ(stack.undoText(), "volume");

    ASSERT_TRUE(stack.undo());
    EXPECT_EQ(volume, 0);
    EXPECT_FALSE(stack.canUndo());
    ASSERT_TRUE(stack.canRedo());

    ASSERT_TRUE(stack.redo());
    EXPECT_EQ(volume, 30);
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStackTest, MergeOnlyMergesCompatibleTypes) {
    UndoStack stack;
    std::vector<int> log;

    stack.push(std::make_unique<ValueCommand>("value", log, 1));
    stack.push(
        std::make_unique<FunctionCommand>("function", [] {}, [] {}));
    EXPECT_EQ(stack.count(), 2u);
}

TEST(UndoStackTest, MacroActsAsSingleStep) {
    UndoStack stack;
    std::vector<int> log;

    stack.beginMacro("multi move");
    EXPECT_TRUE(stack.isMacroActive());

    for (int i = 1; i <= 4; ++i) {
        stack.push(std::make_unique<ValueCommand>("child", log, i));
    }
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(stack.count(), 0u);

    EXPECT_TRUE(stack.endMacro());
    EXPECT_FALSE(stack.isMacroActive());

    ASSERT_EQ(stack.count(), 1u);
    EXPECT_EQ(stack.index(), 1u);
    EXPECT_EQ(stack.undoText(), "multi move");

    EXPECT_TRUE(stack.undo());
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(stack.count(), 1u);

    EXPECT_TRUE(stack.redo());
    EXPECT_EQ(log, (std::vector<int>{1, 2, 3, 4}));
}

TEST(UndoStackTest, EndMacroWithoutChildrenAddsNoStep) {
    UndoStack stack;
    std::vector<int> log;

    stack.beginMacro("empty");
    EXPECT_TRUE(stack.endMacro());
    EXPECT_EQ(stack.count(), 0u);
    EXPECT_FALSE(stack.canUndo());

    stack.beginMacro("real");
    stack.push(std::make_unique<ValueCommand>("x", log, 1));
    EXPECT_TRUE(stack.endMacro());
    stack.beginMacro("abandoned");
    EXPECT_TRUE(stack.endMacro());
    EXPECT_EQ(stack.count(), 1u);
}

TEST(UndoStackTest, EndMacroWithoutBeginFails) {
    UndoStack stack;
    EXPECT_FALSE(stack.endMacro());
}

TEST(UndoStackTest, OperationsIgnoredWhileMacroActive) {
    UndoStack stack;
    std::vector<int> log;

    stack.push(std::make_unique<ValueCommand>("pre", log, 1));
    stack.beginMacro("active");
    EXPECT_FALSE(stack.undo());
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_TRUE(stack.undoText().empty());
    stack.endMacro();

    EXPECT_TRUE(stack.canUndo());
}

TEST(UndoStackTest, MemoryLimitEvictsOldest) {
    UndoStack stack;
    std::vector<int> log;

    stack.setMemoryLimit(20000);
    EXPECT_EQ(stack.memoryLimit(), 20000u);

    for (int i = 0; i < 100; ++i) {
        auto cmd = std::make_unique<ValueCommand>("big" + std::to_string(i),
                                                  log, i);
        cmd->setExtraBytes(4096);
        stack.push(std::move(cmd));
    }

    EXPECT_GE(stack.count(), 1u);
    EXPECT_LT(stack.count(), 100u);
    EXPECT_EQ(stack.index(), stack.count());
    EXPECT_LE(stack.bytesUsed(), 20000u + 8192u);

    const int appliedBefore = static_cast<int>(log.size());
    EXPECT_EQ(appliedBefore, 100);

    const size_t undoable = stack.count();
    for (size_t i = 0; i < undoable; ++i) {
        ASSERT_TRUE(stack.undo());
    }
    EXPECT_FALSE(stack.canUndo());
    EXPECT_EQ(log.size(), static_cast<size_t>(appliedBefore - undoable));

    for (size_t i = 0; i < undoable; ++i) {
        ASSERT_TRUE(stack.redo());
    }
    EXPECT_EQ(log.size(), static_cast<size_t>(appliedBefore));
}

TEST(UndoStackTest, UnlimitedByDefault) {
    UndoStack stack;
    std::vector<int> log;

    for (int i = 0; i < 500; ++i) {
        stack.push(std::make_unique<ValueCommand>("c" + std::to_string(i), log,
                                                  i));
    }
    EXPECT_EQ(stack.count(), 500u);
}

TEST(UndoStackTest, CallbacksFireOnEveryStateChange) {
    UndoStack stack;
    std::vector<int> log;
    int calls = 0;
    stack.setStateCallback([&calls] { ++calls; });

    stack.push(std::make_unique<ValueCommand>("a", log, 1));
    stack.push(std::make_unique<ValueCommand>("b", log, 2));
    stack.undo();
    stack.redo();
    stack.clear();

    EXPECT_EQ(calls, 5);
}

TEST(UndoStackTest, ClearForgetsHistoryButKeepsDocumentState) {
    UndoStack stack;
    std::vector<int> log;

    for (int i = 0; i < 5; ++i) {
        stack.push(std::make_unique<ValueCommand>("c" + std::to_string(i), log,
                                                  i));
    }
    stack.undo();
    stack.undo();
    ASSERT_EQ(log.size(), 3u);
    const size_t bytesBeforeClear = stack.bytesUsed();
    EXPECT_GT(bytesBeforeClear, 0u);

    stack.clear();

    EXPECT_EQ(stack.count(), 0u);
    EXPECT_EQ(stack.index(), 0u);
    EXPECT_EQ(stack.bytesUsed(), 0u);
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
    EXPECT_EQ(log.size(), 3u);
}

TEST(UndoStackTest, NullPushRejected) {
    UndoStack stack;
    EXPECT_FALSE(stack.push(nullptr));
}

TEST(ThreadSafetyTest, UndoStackConcurrentPushUndoRedoStaysConsistent) {
    UndoStack stack;
    std::atomic<bool> go{false};
    std::atomic<int> violations{0};

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 300;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kOpsPerThread; ++i) {
                switch ((t + i) % 3) {
                    case 0: {
                        auto state = std::make_shared<int>(0);
                        auto cmd = std::make_unique<FunctionCommand>(
                            "cmd_" + std::to_string(t) + "_" +
                                std::to_string(i),
                            [state] { ++(*state); },
                            [state] { --(*state); });
                        stack.push(std::move(cmd));
                        break;
                    }
                    case 1:
                        if (stack.undo()) {
                        }
                        break;
                    default:
                        stack.redo();
                        break;
                }
                const UndoStack::State snapshot = stack.state();
                if (snapshot.index > snapshot.count) {
                    violations.fetch_add(1);
                }
            }
        });
    }

    go.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();

    EXPECT_EQ(violations.load(), 0);
    EXPECT_LE(stack.index(), stack.count());
}

} // namespace
