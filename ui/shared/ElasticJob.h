// ElasticJob: runs SetClipElasticCommands. A render that would take longer
// than a moment goes to a background thread behind a cancellable progress
// window (the decision uses the render speed measured so far, so a Debug
// build or a slow machine backgrounds sooner); short ones render inline.
// runMany renders a batch and applies it as one undo step.
#pragma once
#include <Elastic.h>
#include <ClipEdits.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
namespace beatmaker::ui
{
class ElasticJob final : public juce::ThreadWithProgressWindow
{
public:
    // Source length above which a render always goes to the background (seconds).
    static inline double asyncThresholdSeconds = 8.0;
    // A render estimated to take longer than this goes to the background (seconds of wall time).
    static inline double asyncWaitSeconds = 0.15;
    // Source seconds rendered per wall second, learned from every render (0 = unknown yet).
    static inline double renderRate = 0.0;

    static double sourceSeconds (const model::SetClipElasticCommand& cmd) { return cmd.getSampleRate() > 0.0 ? (double) cmd.getSourceLength() / cmd.getSampleRate() : 0.0; }
    static double estimatedSeconds (const model::SetClipElasticCommand& cmd) { return sourceSeconds (cmd) / (renderRate > 0.0 ? renderRate : 15.0); }
    static bool shouldBackground (double sourceSecs) { return sourceSecs > asyncThresholdSeconds || sourceSecs / (renderRate > 0.0 ? renderRate : 15.0) > asyncWaitSeconds; }
    static void learn (double sourceSecs, double wallSecs)
    {
        if (wallSecs <= 1.0e-4 || sourceSecs <= 0.0) return;
        const double rate = sourceSecs / wallSecs;
        renderRate = renderRate > 0.0 ? renderRate * 0.7 + rate * 0.3 : rate;
    }
    static bool renderTimed (model::SetClipElasticCommand& cmd, const engine::TimeStretch::ProgressFn& progress = {})
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool ok = cmd.render (progress);
        learn (sourceSeconds (cmd), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        return ok;
    }

    // Executes `cmd` on `session` now or after a background render. `done` receives true when the command was applied.
    static void run (model::Session& session, std::unique_ptr<model::SetClipElasticCommand> cmd, std::function<void (bool applied)> done = {})
    {
        if (cmd == nullptr || cmd->wasCancelled()) { if (done) done (false); return; }
        if (cmd->isRendered() || ! shouldBackground (sourceSeconds (*cmd)))
        {
            if (! cmd->isRendered()) renderTimed (*cmd);
            auto* raw = cmd.get();
            const bool ok = raw->isRendered();
            if (ok) session.execute (std::move (cmd));
            if (done) done (ok && ! raw->wasStale());
            return;
        }
        std::vector<std::unique_ptr<model::SetClipElasticCommand>> one; one.push_back (std::move (cmd));
        auto* job = new ElasticJob (session, std::move (one), juce::String(), juce::String(), [done] (int applied) { if (done) done (applied > 0); });
        job->launchThread();
    }

    // Renders a batch (inline when quick, else behind one progress window) and applies the
    // rendered commands as one undo step named `compoundName`. `done` gets the number applied.
    static void runMany (model::Session& session, std::vector<std::unique_ptr<model::SetClipElasticCommand>> cmds, const juce::String& title,
                         const juce::String& compoundName, std::function<void (int applied)> done = {})
    {
        double total = 0.0;
        for (auto& c : cmds) if (c != nullptr && ! c->wasCancelled() && ! c->isRendered()) total += sourceSeconds (*c);
        if (cmds.empty()) { if (done) done (0); return; }
        if (! shouldBackground (total))
        {
            for (auto& c : cmds) if (c != nullptr && ! c->wasCancelled() && ! c->isRendered()) renderTimed (*c);
            const int n = apply (session, std::move (cmds), compoundName);
            if (done) done (n);
            return;
        }
        (new ElasticJob (session, std::move (cmds), title, compoundName, std::move (done)))->launchThread();
    }

private:
    ElasticJob (model::Session& s, std::vector<std::unique_ptr<model::SetClipElasticCommand>> c, const juce::String& title, juce::String compound, std::function<void (int)> d)
        : ThreadWithProgressWindow (title.isEmpty() && ! c.empty() && c.front() != nullptr ? "Rendering " + c.front()->getName() + "..." : title, true, true),
          session (s), cmds (std::move (c)), compoundName (std::move (compound)), done (std::move (d)) {}

    static int apply (model::Session& session, std::vector<std::unique_ptr<model::SetClipElasticCommand>> cmds, const juce::String& compoundName)
    {
        int n = 0;
        if (compoundName.isEmpty() && cmds.size() == 1)
        {
            auto* raw = cmds.front().get();
            if (raw != nullptr && raw->isRendered()) { session.execute (std::move (cmds.front())); n = raw->wasStale() ? 0 : 1; }
            return n;
        }
        auto compound = std::make_unique<model::CompoundCommand> (compoundName.isEmpty() ? juce::String ("Elastic Audio") : compoundName);
        for (auto& c : cmds) if (c != nullptr && c->isRendered()) { compound->add (std::move (c)); ++n; }
        if (n > 0) session.execute (std::move (compound));
        return n;
    }
    void run() override
    {
        double total = 0.0, doneSecs = 0.0;
        for (auto& c : cmds) if (c != nullptr && ! c->wasCancelled()) total += sourceSeconds (*c);
        for (auto& c : cmds)
        {
            if (threadShouldExit()) break;
            if (c == nullptr || c->wasCancelled() || c->isRendered()) continue;
            const double mine = sourceSeconds (*c);
            renderTimed (*c, [this, &doneSecs, mine, total] (double p) { setProgress (total > 0.0 ? (doneSecs + p * mine) / total : p); return ! threadShouldExit(); });
            doneSecs += mine;
        }
    }
    void threadComplete (bool userCancelled) override
    {
        int applied = 0;
        if (! userCancelled) applied = apply (session, std::move (cmds), compoundName);
        if (done) done (applied);
        juce::MessageManager::callAsync ([this] { delete this; });
    }
    model::Session& session;
    std::vector<std::unique_ptr<model::SetClipElasticCommand>> cmds;
    juce::String compoundName;
    std::function<void (int)> done;
};
} // namespace beatmaker::ui
