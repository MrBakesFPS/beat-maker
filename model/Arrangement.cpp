#include "Arrangement.h"
#include <map>

namespace beatmaker::model
{

void Arrangement::shiftClips (const Session& s, double from, double to, double delta, CompoundCommand& out)
{
    for (int t = 0; t < s.getNumTracks(); ++t)
        for (const auto& ref : ClipEdits::allClips (s, t))
        {
            const auto timing = ClipEdits::timing (s, ref);
            if (! timing) continue;
            const double start = (double) timing->start / timing->sampleRate;
            if (start < from - 1e-9 || start >= to - 1e-9) continue;
            const juce::int64 newStart = juce::jmax<juce::int64> (0, (juce::int64) std::llround ((start + delta) * timing->sampleRate));
            out.add (std::make_unique<MoveClipCommand> (ref, ref.track, newStart));
        }
}

namespace
{
    Marker shifted (Marker m, double delta) { m.seconds += delta; m.endSeconds += delta; if (m.recallSelection) { m.selectionStart += delta; m.selectionEnd += delta; } return m; }
}

std::unique_ptr<Command> Arrangement::moveSection (const Session& s, int markerId, bool later)
{
    const auto sections = s.getSections();
    int index = -1;
    for (int i = 0; i < (int) sections.size(); ++i) if (sections[(size_t) i]->id == markerId) index = i;
    if (index < 0) return nullptr;
    const int other = later ? index + 1 : index - 1;
    if (! juce::isPositiveAndBelow (other, (int) sections.size())) return nullptr;

    const Marker a = *sections[(size_t) juce::jmin (index, other)];   // earlier of the two
    const Marker b = *sections[(size_t) juce::jmax (index, other)];   // later
    // After the swap: b starts where a started; a follows b.
    const double gap = b.seconds - a.endSeconds;                     // any space between them travels with b... keep it between
    const double newBStart = a.seconds;
    const double newAStart = a.seconds + b.length() + gap;

    auto compound = std::make_unique<CompoundCommand> ("Move Section");
    shiftClips (s, a.seconds, a.endSeconds, newAStart - a.seconds, *compound);
    shiftClips (s, b.seconds, b.endSeconds, newBStart - b.seconds, *compound);
    compound->add (std::make_unique<ReplaceMarkerCommand> (shifted (a, newAStart - a.seconds), "Move Section"));
    compound->add (std::make_unique<ReplaceMarkerCommand> (shifted (b, newBStart - b.seconds), "Move Section"));
    return compound;
}

std::unique_ptr<Command> Arrangement::duplicateSection (const Session& s, int markerId)
{
    const auto* m = s.getMarker (markerId);
    if (m == nullptr || ! m->isSection || ! m->isRange()) return nullptr;
    const double len = m->length();
    auto compound = std::make_unique<CompoundCommand> ("Duplicate Section");

    // Later material (and later markers) moves right first, from the back so refs stay valid.
    shiftClips (s, m->endSeconds, 1.0e12, len, *compound);
    for (const auto& other : s.getMarkers())
        if (other.id != m->id && other.seconds >= m->endSeconds - 1e-9)
            compound->add (std::make_unique<ReplaceMarkerCommand> (shifted (other, len), "Duplicate Section"));

    // Copies of the clips inside the section, placed one section later. Each
    // duplicate is appended to its list, so the k-th copy on a track lands at count + k.
    std::map<std::pair<int, int>, int> appended;
    for (int t = 0; t < s.getNumTracks(); ++t)
        for (const auto& ref : ClipEdits::allClips (s, t))
        {
            const auto timing = ClipEdits::timing (s, ref);
            if (! timing) continue;
            const double start = (double) timing->start / timing->sampleRate;
            if (start < m->seconds - 1e-9 || start >= m->endSeconds - 1e-9) continue;
            compound->add (std::make_unique<DuplicateClipCommand> (ref));
            const int newIndex = ClipEdits::numClips (*s.getTrack (t), ref.kind) + appended[{ t, (int) ref.kind }]++;
            compound->add (std::make_unique<MoveClipCommand> (ClipRef { t, ref.kind, newIndex }, t,
                                                              (juce::int64) std::llround ((start + len) * timing->sampleRate)));
        }

    Marker copy = *m;
    copy.id = 0;
    copy.seconds += len; copy.endSeconds += len;
    if (copy.recallSelection) { copy.selectionStart += len; copy.selectionEnd += len; }
    compound->add (std::make_unique<AddMarkerCommand> (copy));
    return compound;
}

std::unique_ptr<Command> Arrangement::deleteSectionTime (const Session& s, int markerId)
{
    const auto* m = s.getMarker (markerId);
    if (m == nullptr || ! m->isSection || ! m->isRange()) return nullptr;
    const double len = m->length();
    auto compound = std::make_unique<CompoundCommand> ("Delete Section");

    // Clips inside the section go; later ones move left; later markers too.
    std::vector<ClipRef> inside;
    for (int t = 0; t < s.getNumTracks(); ++t)
        for (const auto& ref : ClipEdits::allClips (s, t))
        {
            const auto timing = ClipEdits::timing (s, ref);
            if (! timing) continue;
            const double start = (double) timing->start / timing->sampleRate;
            if (start >= m->seconds - 1e-9 && start < m->endSeconds - 1e-9) inside.push_back (ref);
        }
    shiftClips (s, m->endSeconds, 1.0e12, -len, *compound);
    // Remove from the highest index down per track so earlier refs stay valid.
    std::sort (inside.begin(), inside.end(), [] (const ClipRef& a, const ClipRef& b) { return a.track != b.track ? a.track < b.track : a.index > b.index; });
    for (const auto& ref : inside) compound->add (std::make_unique<RemoveAnyClipCommand> (ref));
    for (const auto& other : s.getMarkers())
        if (other.id != m->id && other.seconds >= m->endSeconds - 1e-9)
            compound->add (std::make_unique<ReplaceMarkerCommand> (shifted (other, -len), "Delete Section"));
    compound->add (std::make_unique<RemoveMarkerCommand> (m->id));
    return compound;
}

} // namespace beatmaker::model
