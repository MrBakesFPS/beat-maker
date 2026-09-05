#include "GroupLogic.h"

namespace beatmaker::model
{

namespace
{
    std::vector<int> membersWhere (const Session& s, int trackIndex, bool (*pred) (const Group&))
    {
        std::vector<int> result { trackIndex };
        auto* t = s.getTrack (trackIndex);
        if (t == nullptr) return result;
        for (const auto& g : s.getGroups())
        {
            if (! g.active || ! pred (g) || ! g.contains (t->id)) continue;
            for (int id : g.trackIds)
            {
                const int idx = s.indexOfTrackId (id);
                if (idx >= 0 && std::find (result.begin(), result.end(), idx) == result.end()) result.push_back (idx);
            }
        }
        return result;
    }

    bool groupFollows (const Session& s, int trackIndex, int memberIndex, bool Group::Attributes::* attr)
    {
        auto* a = s.getTrack (trackIndex); auto* b = s.getTrack (memberIndex);
        if (a == nullptr || b == nullptr) return false;
        for (const auto& g : s.getGroups())
            if (g.active && g.isMix() && g.contains (a->id) && g.contains (b->id) && (g.attributes.*attr)) return true;
        return false;
    }
}

std::vector<int> GroupLogic::mixMembers (const Session& s, int trackIndex)  { return membersWhere (s, trackIndex, [] (const Group& g) { return g.isMix(); }); }
std::vector<int> GroupLogic::editMembers (const Session& s, int trackIndex) { return membersWhere (s, trackIndex, [] (const Group& g) { return g.isEdit(); }); }

std::vector<const Group*> GroupLogic::groupsOf (const Session& s, int trackId)
{
    std::vector<const Group*> out;
    for (const auto& g : s.getGroups()) if (g.contains (trackId)) out.push_back (&g);
    return out;
}

std::unique_ptr<Command> GroupLogic::mixCommand (const Session& s, int trackIndex, float newGain, float newPan)
{
    auto* t = s.getTrack (trackIndex);
    if (t == nullptr) return nullptr;

    const auto members = mixMembers (s, trackIndex);
    if (members.size() == 1)
        return std::make_unique<SetTrackMixCommand> (trackIndex, newGain, newPan);

    const float deltaDb = juce::Decibels::gainToDecibels (newGain, -100.0f) - juce::Decibels::gainToDecibels (t->gain, -100.0f);
    const bool panChanged = std::abs (newPan - t->pan) > 1.0e-6f;

    auto compound = std::make_unique<CompoundCommand> ("Grouped Volume/Pan");
    compound->add (std::make_unique<SetTrackMixCommand> (trackIndex, newGain, newPan));
    for (int m : members)
    {
        if (m == trackIndex) continue;
        auto* mt = s.getTrack (m);
        if (mt == nullptr) continue;
        float gain = mt->gain, pan = mt->pan;
        if (groupFollows (s, trackIndex, m, &Group::Attributes::volume))
        {
            // Relative in dB, like Pro Tools; a track at -inf stays there, and clamps at +6 dB.
            if (newGain <= 0.0001f) gain = 0.0f;
            else if (mt->gain > 0.0001f) gain = juce::jlimit (0.0f, 2.0f, juce::Decibels::decibelsToGain (juce::Decibels::gainToDecibels (mt->gain) + deltaDb));
        }
        if (panChanged && groupFollows (s, trackIndex, m, &Group::Attributes::pan)) pan = newPan;
        compound->add (std::make_unique<SetTrackMixCommand> (m, gain, pan));
    }
    return compound;
}

std::unique_ptr<Command> GroupLogic::flagCommand (const Session& s, int trackIndex, SetTrackFlagCommand::Flag flag, bool value)
{
    const auto members = mixMembers (s, trackIndex);
    bool Group::Attributes::* attr = flag == SetTrackFlagCommand::Flag::mute ? &Group::Attributes::mute
                                   : flag == SetTrackFlagCommand::Flag::solo ? &Group::Attributes::solo
                                   : flag == SetTrackFlagCommand::Flag::arm  ? &Group::Attributes::arm : nullptr;
    if (members.size() == 1 || attr == nullptr)
        return std::make_unique<SetTrackFlagCommand> (trackIndex, flag, value);

    auto compound = std::make_unique<CompoundCommand> ("Grouped " + SetTrackFlagCommand (trackIndex, flag, value).getName());
    for (int m : members)
        if (m == trackIndex || groupFollows (s, trackIndex, m, attr))
            compound->add (std::make_unique<SetTrackFlagCommand> (m, flag, value));
    return compound;
}

std::vector<ClipRef> GroupLogic::siblingsOf (const Session& s, const ClipRef& ref)
{
    std::vector<ClipRef> out;
    auto t = ClipEdits::timing (s, ref);
    if (! t) return out;
    for (int m : editMembers (s, ref.track))
    {
        if (m == ref.track) continue;
        for (const auto& other : ClipEdits::allClips (s, m))
            if (auto ot = ClipEdits::timing (s, other); ot && ot->start == t->start) out.push_back (other);
    }
    return out;
}

std::unique_ptr<Command> GroupLogic::moveCommand (const Session& s, const ClipRef& ref, int newTrack, juce::int64 newStart)
{
    auto t = ClipEdits::timing (s, ref);
    const auto siblings = siblingsOf (s, ref);
    if (! t || siblings.empty() || newTrack != ref.track)
        return std::make_unique<MoveClipCommand> (ref, newTrack, newStart);

    const juce::int64 delta = newStart - t->start;
    auto compound = std::make_unique<CompoundCommand> ("Grouped Move");
    compound->add (std::make_unique<MoveClipCommand> (ref, ref.track, newStart));
    for (const auto& sib : siblings)
        if (auto st = ClipEdits::timing (s, sib))
            compound->add (std::make_unique<MoveClipCommand> (sib, sib.track, st->start + delta));
    return compound;
}

std::unique_ptr<Command> GroupLogic::trimCommand (const Session& s, const ClipRef& ref, juce::int64 newStart, juce::int64 newLength)
{
    auto t = ClipEdits::timing (s, ref);
    const auto siblings = siblingsOf (s, ref);
    if (! t || siblings.empty())
        return std::make_unique<TrimClipCommand> (ref, newStart, newLength);

    const juce::int64 startDelta = newStart - t->start, endDelta = (newStart + newLength) - (t->start + t->length);
    auto compound = std::make_unique<CompoundCommand> ("Grouped Trim");
    compound->add (std::make_unique<TrimClipCommand> (ref, newStart, newLength));
    for (const auto& sib : siblings)
        if (auto st = ClipEdits::timing (s, sib))
        {
            const juce::int64 ns = st->start + startDelta, ne = st->start + st->length + endDelta;
            if (ne > ns) compound->add (std::make_unique<TrimClipCommand> (sib, ns, ne - ns));
        }
    return compound;
}

} // namespace beatmaker::model
