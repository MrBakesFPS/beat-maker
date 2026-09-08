#include <catch2/catch_test_macros.hpp>
#include <StepEdits.h>

using namespace beatmaker;
using model::StepEdits;
using model::StepCell;

static engine::StepPattern makePattern()
{
    engine::StepPattern p; p.numSteps = 16; p.stepsPerBeat = 4;
    p.set (0, 0, 110); p.set (0, 8, 100); p.set (1, 4, 90); p.set (1, 12, 90); p.set (4, 2, 60); p.set (4, 6, 60);
    return p;
}

TEST_CASE ("StepEdits: selection, clear, velocity, shift")
{
    const auto p = makePattern();
    CHECK (StepEdits::all (p).size() == 6);
    CHECK (StepEdits::inRegion (p, 0, 7, 0, 1) == std::vector<StepCell> { { 0, 0 }, { 1, 4 } });
    CHECK (StepEdits::inRegion (p, 7, 0, 1, 0) == std::vector<StepCell> { { 0, 0 }, { 1, 4 } });   // either order
    CHECK (StepEdits::contains (StepEdits::all (p), { 4, 6 }));
    CHECK (! StepEdits::contains (StepEdits::all (p), { 4, 7 }));

    const auto cleared = StepEdits::clear (p, { { 0, 0 }, { 4, 2 } });
    CHECK (cleared.get (0, 0) == 0); CHECK (cleared.get (4, 2) == 0); CHECK (cleared.get (0, 8) == 100);
    const auto louder = StepEdits::changeVelocity (p, { { 0, 0 }, { 4, 2 }, { 4, 7 } }, 30);
    CHECK (louder.get (0, 0) == 127); CHECK (louder.get (4, 2) == 90); CHECK (louder.get (4, 7) == 0);   // silent cells stay silent
    const auto setv = StepEdits::setVelocity (p, { { 1, 4 } }, 300);
    CHECK (setv.get (1, 4) == 127);

    StepEdits::Cells moved;
    const auto shifted = StepEdits::shift (p, { { 0, 0 }, { 1, 4 } }, 2, 1, &moved);
    CHECK (shifted.get (0, 0) == 0); CHECK (shifted.get (1, 2) == 110); CHECK (shifted.get (2, 6) == 90);
    CHECK (moved == std::vector<StepCell> { { 1, 2 }, { 2, 6 } });
    const auto clamped = StepEdits::shift (p, { { 0, 0 }, { 0, 8 } }, -5, 0, &moved);   // the group cannot go below step 0
    CHECK (clamped.get (0, 0) == 110); CHECK (clamped.get (0, 8) == 100);
    CHECK (moved == std::vector<StepCell> { { 0, 0 }, { 0, 8 } });
    const auto atEnd = StepEdits::shift (p, { { 1, 12 } }, 10, 0, &moved);
    CHECK (moved == std::vector<StepCell> { { 1, 15 } });
    CHECK (atEnd.get (1, 15) == 90);
}

TEST_CASE ("StepEdits: copy, paste, duplicate, unroll, resize")
{
    const auto p = makePattern();
    const auto hits = StepEdits::copy (p, { { 1, 4 }, { 0, 8 }, { 4, 6 } });
    REQUIRE (hits.size() == 3);
    CHECK (hits[0].step == 0); CHECK (hits[0].pad == 1);     // relative to the earliest, sorted
    CHECK (hits[2].step == 4); CHECK (hits[2].velocity == 100);
    StepEdits::Cells pasted;
    const auto pastedPattern = StepEdits::paste (p, hits, 10, &pasted);
    CHECK (pastedPattern.get (1, 10) == 90); CHECK (pastedPattern.get (4, 12) == 60); CHECK (pastedPattern.get (0, 14) == 100);
    CHECK (pasted.size() == 3);
    const auto partial = StepEdits::paste (p, hits, 14, &pasted);   // steps past the end are dropped
    CHECK (pasted.size() == 1);
    CHECK (partial.get (1, 14) == 90);

    const auto dup = StepEdits::duplicate (p, { { 4, 2 }, { 4, 6 } }, &pasted);   // after step 6 -> 7 and 11
    CHECK (dup.get (4, 7) == 60); CHECK (dup.get (4, 11) == 60);
    CHECK (pasted == std::vector<StepCell> { { 4, 7 }, { 4, 11 } });

    const auto unrolled = StepEdits::unroll (p, 40);
    CHECK (unrolled.numSteps == 40);
    CHECK (unrolled.get (0, 16) == 110); CHECK (unrolled.get (1, 28) == 90); CHECK (unrolled.get (0, 32) == 110); CHECK (unrolled.get (4, 38) == 60);
    const auto capped = StepEdits::unroll (p, 10000);
    CHECK (capped.numSteps == engine::StepPattern::maxSteps);
    const auto bigger = StepEdits::resize (p, 32);
    CHECK (bigger.numSteps == 32); CHECK (bigger.get (0, 16) == 0); CHECK (bigger.get (0, 0) == 110);
    const auto smaller = StepEdits::resize (p, 8);
    CHECK (smaller.numSteps == 8);
}

TEST_CASE ("StepEdits: held hits (lengths) stretch up to the next hit, and move, copy and unroll with the hit")
{
    auto p = makePattern();   // pad 0 hits at 0 and 8; pad 4 at 2 and 6
    CHECK (p.getLength (0, 0) == 1);
    CHECK (StepEdits::maxLength (p, { 0, 0 }) == 8);      // room before the hit at 8
    CHECK (StepEdits::maxLength (p, { 0, 8 }) == 8);      // room before the end
    CHECK (StepEdits::maxLength (p, { 4, 2 }) == 4);
    const auto held = StepEdits::setLength (p, { { 0, 0 }, { 4, 2 }, { 4, 7 } }, 6);
    CHECK (held.getLength (0, 0) == 6); CHECK (held.getLength (4, 2) == 4); CHECK (held.getLength (4, 7) == 1);   // capped; silent cell untouched
    const auto longer = StepEdits::stretch (held, { { 0, 0 } }, 5);
    CHECK (longer.getLength (0, 0) == 8);
    const auto shorter = StepEdits::stretch (held, { { 0, 0 } }, -10);
    CHECK (shorter.getLength (0, 0) == 1);
    // The span covers its steps: the hit at 0 held for 6 covers step 5, not 6
    CHECK (StepEdits::hitCovering (held, 0, 5).step == 0);
    CHECK (StepEdits::hitCovering (held, 0, 6).step == -1);
    CHECK (StepEdits::hitCovering (held, 0, 8).step == 8);
    CHECK (StepEdits::hitCovering (held, 3, 0).step == -1);
    // Move keeps the length, cut at the pattern's end; clear resets it
    StepEdits::Cells moved;
    const auto shifted = StepEdits::shift (held, { { 0, 0 } }, 12, 0, &moved);
    CHECK (shifted.getLength (0, 12) == 4); CHECK (shifted.getLength (0, 0) == 1);
    CHECK (StepEdits::clear (held, { { 0, 0 } }).getLength (0, 0) == 1);
    // Copy, paste and duplicate carry it
    const auto hits = StepEdits::copy (held, { { 0, 0 } });
    REQUIRE (hits.size() == 1); CHECK (hits[0].length == 6);
    const auto pasted = StepEdits::paste (held, hits, 9);
    CHECK (pasted.getLength (0, 9) == 6);
    // Unroll repeats the length; a shrink cuts it
    const auto un = StepEdits::unroll (held, 32);
    CHECK (un.getLength (0, 16) == 6);
    CHECK (StepEdits::resize (held, 4).getLength (0, 0) == 4);
    CHECK (StepEdits::resize (held, 32).getLength (0, 0) == 6);
}

TEST_CASE ("StepEdits: a hit moved onto another swaps places with it, so nothing is lost")
{
    auto p = makePattern();   // pad 0 hits at 0 (110) and 8 (100); pad 1 at 4 and 12; pad 4 at 2 and 6
    StepEdits::Cells moved;
    const auto onto = StepEdits::shift (p, { { 0, 0 } }, 8, 0, &moved);
    CHECK (onto.get (0, 8) == 110); CHECK (onto.get (0, 0) == 100);   // swapped
    CHECK (moved == std::vector<StepCell> { { 0, 8 } });
    CHECK (StepEdits::all (onto).size() == 6);
    // Moving to another pad displaces the hit there back to the vacated cell
    const auto toPad = StepEdits::shift (p, { { 0, 8 } }, 4, 1, &moved);   // (0,8) -> (1,12), which holds a 90
    CHECK (toPad.get (1, 12) == 100); CHECK (toPad.get (0, 8) == 90);
    // A run shuffles: hits at 0, 1, 2 on pad 2; moving {0, 1} right by one puts the hit from 2 at 0
    engine::StepPattern run; run.numSteps = 16; run.stepsPerBeat = 4;
    run.set (2, 0, 110); run.set (2, 1, 105); run.set (2, 2, 70);
    const auto shuffled = StepEdits::shift (run, { { 2, 0 }, { 2, 1 } }, 1, 0, &moved);
    CHECK (shuffled.get (2, 0) == 70); CHECK (shuffled.get (2, 1) == 110); CHECK (shuffled.get (2, 2) == 105);
    CHECK (moved == std::vector<StepCell> { { 2, 1 }, { 2, 2 } });
    // Held hits are cut where they would run into the hit that moved in front of them
    auto held = StepEdits::setLength (p, { { 0, 0 } }, 6);
    const auto inFront = StepEdits::shift (held, { { 1, 4 } }, -1, -1, &moved);   // (1,4) -> (0,3)
    CHECK (inFront.get (0, 3) == 90); CHECK (inFront.getLength (0, 0) == 3);
    // Arrow-key style: one step at a time down a column passes over a hit and leaves it behind
    auto walk = run;
    StepEdits::Cells sel { { 2, 0 } };
    walk = StepEdits::shift (walk, sel, 1, 0, &sel);   // onto step 1: swap
    walk = StepEdits::shift (walk, sel, 1, 0, &sel);   // onto step 2: swap
    CHECK (sel == std::vector<StepCell> { { 2, 2 } });
    CHECK (walk.get (2, 2) == 110); CHECK (walk.get (2, 0) == 105); CHECK (walk.get (2, 1) == 70);
}
