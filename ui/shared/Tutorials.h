// Tutorials: task lists whose steps tick themselves off by inspecting the
// session/transport, with a command to "show me" each step.
#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace beatmaker::ui
{

struct TutorialStep
{
    juce::String title, hint, commandId;   // commandId: run to perform the step (may be empty)
    std::function<bool()> isDone;
};

struct Tutorial
{
    juce::String title, summary;
    std::vector<TutorialStep> steps;

    int doneCount() const { int n = 0; for (const auto& s : steps) if (s.isDone && s.isDone()) ++n; return n; }
    bool isComplete() const { return ! steps.empty() && doneCount() == (int) steps.size(); }
    // First step not yet done (-1 when complete)
    int currentStep() const { for (int i = 0; i < (int) steps.size(); ++i) if (! (steps[(size_t) i].isDone && steps[(size_t) i].isDone())) return i; return -1; }
};

} // namespace beatmaker::ui
