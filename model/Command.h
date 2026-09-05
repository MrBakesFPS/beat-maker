// Command layer: every mutation of the Session is a Command so it can be
// undone, redone, scripted, and (later) synced. The UI never edits the
// Session directly.
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace beatmaker::model
{

class Session;

class Command
{
public:
    virtual ~Command() = default;
    virtual juce::String getName() const = 0;
    virtual void execute (Session&) = 0;
    virtual void undo (Session&) = 0;
};

class CommandHistory
{
public:
    void execute (Session& session, std::unique_ptr<Command> command)
    {
        command->execute (session);
        undoStack.push_back (std::move (command));
        redoStack.clear();
    }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }

    juce::String getUndoName() const { return canUndo() ? undoStack.back()->getName() : juce::String(); }
    juce::String getRedoName() const { return canRedo() ? redoStack.back()->getName() : juce::String(); }

    bool undo (Session& session)
    {
        if (! canUndo()) return false;
        auto cmd = std::move (undoStack.back()); undoStack.pop_back();
        cmd->undo (session);
        redoStack.push_back (std::move (cmd));
        return true;
    }

    bool redo (Session& session)
    {
        if (! canRedo()) return false;
        auto cmd = std::move (redoStack.back()); redoStack.pop_back();
        cmd->execute (session);
        undoStack.push_back (std::move (cmd));
        return true;
    }

    void clear() { undoStack.clear(); redoStack.clear(); }

private:
    std::vector<std::unique_ptr<Command>> undoStack, redoStack;
};

} // namespace beatmaker::model
