// Converts the Session document into an engine RenderSnapshot, applying
// mute/solo and gain staging. Runs on the message thread.
#pragma once

#include "Session.h"
#include <graph/RenderSnapshot.h>

namespace beatmaker::model
{

std::unique_ptr<engine::RenderSnapshot> buildRenderSnapshot (const Session& session);

} // namespace beatmaker::model
