// ElasticJob: runs a SetClipElasticCommand. Short clips render inline; long
// ones render on a background thread behind a cancellable progress window,
// then the command is applied on the message thread.
#pragma once

#include <Elastic.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace beatmaker::ui
{

class ElasticJob final : public juce::ThreadWithProgressWindow
{
public:
    // Source length above which a render goes to the background (seconds).
    static inline double asyncThresholdSeconds = 8.0;

    // Executes `cmd` on `session` now or after a background render. `done`
    // receives true when the command was applied.
    static void run (model::Session& session, std::unique_ptr<model::SetClipElasticCommand> cmd, std::function<void (bool applied)> done = {})
    {
        if (cmd == nullptr || cmd->wasCancelled()) { if (done) done (false); return; }
        const double seconds = cmd->getSampleRate() > 0.0 ? (double) cmd->getSourceLength() / cmd->getSampleRate() : 0.0;
        if (cmd->isRendered() || seconds <= asyncThresholdSeconds)
        {
            if (! cmd->isRendered()) cmd->render();
            auto* raw = cmd.get();
            const bool ok = raw->isRendered();
            if (ok) session.execute (std::move (cmd));
            if (done) done (ok && ! raw->wasStale());
            return;
        }
        // Owns itself until the thread completes.
        auto* job = new ElasticJob (session, std::move (cmd), std::move (done));
        job->launchThread();
    }

private:
    ElasticJob (model::Session& s, std::unique_ptr<model::SetClipElasticCommand> c, std::function<void (bool)> d)
        : ThreadWithProgressWindow ("Rendering " + c->getName() + "...", true, true), session (s), cmd (std::move (c)), done (std::move (d)) {}

    void run() override
    {
        cmd->render ([this] (double p) { setProgress (p); return ! threadShouldExit(); });
    }

    void threadComplete (bool userCancelled) override
    {
        bool applied = false;
        if (! userCancelled && cmd->isRendered())
        {
            auto* raw = cmd.get();
            session.execute (std::move (cmd));
            applied = ! raw->wasStale();
        }
        if (done) done (applied);
        juce::MessageManager::callAsync ([this] { delete this; });
    }

    model::Session& session;
    std::unique_ptr<model::SetClipElasticCommand> cmd;
    std::function<void (bool)> done;
};

} // namespace beatmaker::ui
