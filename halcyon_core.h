#pragma once
#include <opxy/audio.hpp>

namespace hwtone {
struct Voice {
    opxy::u32 random;
    // The tone: one phase drives the sine and the soft saw; the ensemble copy,
    // the late octaves and the chord tones run on their own phases.
    opxy::u32 phase, copy_phase, phase_up, phase_down, phase_fifth, phase_ninth;
    opxy::i32 low, band;               // the saws' state-variable low-pass, Q29
    // Swell: the bloom's position, 0 at the key press to full at the end, Q31.
    opxy::u32 bloom;
    opxy::u32 blocks;                  // blocks since the note started (stops counting at the top)
    opxy::i32 up_level, down_level;    // the late octaves' own fades, Q31
    opxy::i32 fifth_level, ninth_level, copy_level;  // Chord and Ensemble, eased, Q31
    // Drift: a wandering tape speed (two smoothing stages over random
    // targets), the capstan's slow turn, and the filter's slow breath.
    opxy::i32 wander, wander_smooth, wander_target;
    opxy::u32 wander_blocks;
    opxy::u32 capstan;
    opxy::i32 breath, breath_smooth, breath_target;
    opxy::u32 breath_blocks;
    opxy::i32 detune;                  // this voice's own slight offset, Q15 of the full drift
    opxy::u32 chorus;                  // the ensemble's slow detune sweep
    // Hiss: band-limited noise. Age: the tape's rounded top and level wobble.
    opxy::i32 hiss_low, hiss_high;
    opxy::i32 tape;                    // Age's low-pass state, Q29
    opxy::u32 reel;
    opxy::i32 wobble, wobble_target;
    opxy::u32 wobble_blocks;
    opxy::i32 level;                   // last block's output level, Q31, ramped across each block
    // The built-in slow sines (with Drift): each voice's small offset in the
    // shared cycle for Warmth, Air, Hiss, Chord, Ensemble and Age.
    opxy::u32 sway[6];
    opxy::u32 seen;                    // the shared clock as this voice last saw it
    // Halcyon Wander's additions, set by its tone each block: a harmonic seventh
    // (7:4) over the note, and how much deeper Drift's tape wobble goes (Q8, 256 = Halcyon's).
    opxy::u32 phase_seventh;
    opxy::i32 seventh_level, seventh_target;
    opxy::i32 wow;
};
// The voices share one clock for the slow sines, so they keep running from note
// to note: each block, exactly one sounding voice moves it on.
struct Shared {
    opxy::u32 clock;                   // blocks
};
}
