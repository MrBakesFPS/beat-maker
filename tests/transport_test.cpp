#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <transport/Transport.h>

using beatmaker::engine::Transport;

TEST_CASE ("Transport starts stopped at zero")
{
    Transport t;
    CHECK_FALSE (t.isPlaying());
    CHECK (t.getPositionSamples() == 0);
    CHECK (t.getBarBeat().bar == 1);
    CHECK (t.getBarBeat().beat == 1);
}

TEST_CASE ("Transport advances and converts to seconds")
{
    Transport t;
    t.setSampleRate (48000.0);
    t.play();
    t.advance (24000);
    CHECK (t.isPlaying());
    CHECK_THAT (t.getPositionSeconds(), Catch::Matchers::WithinAbs (0.5, 1e-9));
}

TEST_CASE ("Bars and beats at 120 BPM 4/4")
{
    Transport t;
    t.setSampleRate (44100.0);
    t.setBpm (120.0);

    // 120 BPM: one beat = 0.5 s, one bar = 2 s.
    auto bb = t.barBeatForSeconds (0.0);
    CHECK (bb.bar == 1); CHECK (bb.beat == 1); CHECK (bb.tick == 0);

    bb = t.barBeatForSeconds (0.5);
    CHECK (bb.bar == 1); CHECK (bb.beat == 2);

    bb = t.barBeatForSeconds (2.0);
    CHECK (bb.bar == 2); CHECK (bb.beat == 1);

    bb = t.barBeatForSeconds (2.25); // half a beat into bar 2
    CHECK (bb.bar == 2); CHECK (bb.beat == 1); CHECK (bb.tick == Transport::ticksPerBeat / 2);

    bb = t.barBeatForSeconds (7.5);  // bar 4, beat 4
    CHECK (bb.bar == 4); CHECK (bb.beat == 4);
}

TEST_CASE ("Position never goes negative and return-to-start works")
{
    Transport t;
    t.setPositionSamples (-500);
    CHECK (t.getPositionSamples() == 0);
    t.setPositionSeconds (3.0);
    CHECK (t.getPositionSamples() > 0);
    t.returnToStart();
    CHECK (t.getPositionSamples() == 0);
}
