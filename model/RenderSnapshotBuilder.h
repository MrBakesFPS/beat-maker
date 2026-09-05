// Converts the Session document into an engine RenderSnapshot, applying
// mute/solo and gain staging. Runs on the message thread.
#pragma once

#include "Session.h"
#include <graph/RenderSnapshot.h>

namespace beatmaker::model
{

std::unique_ptr<engine::RenderSnapshot> buildRenderSnapshot (const Session& session);

// Processing order so sidechain senders run before the strips they key (exposed for tests).
std::vector<int> computeStripOrder (const std::vector<engine::RenderStrip>& strips);

} // namespace beatmaker::model
