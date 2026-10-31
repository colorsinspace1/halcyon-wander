#pragma once
#include <opxy/audio.hpp>
#include "halcyon_core.h"
#include "journey.h"

namespace hwander {
// One sounding note: a whole Halcyon voice at its own pitch (a Q16 ratio of the
// held key), its loudness (Q15) and its fade (Q31, falling from full).
struct Note {
    hwtone::Voice tone;
    opxy::u32 ratio;      // 0: silent
    opxy::i32 gain;
    opxy::i32 fade;
    // A note given way while still sounding fades out over a few blocks first,
    // then the waiting note starts: no click.
    opxy::u32 dying;      // blocks left of that fade, 0 when not giving way
    opxy::u32 next_ratio;
    opxy::i32 next_gain;
};
const unsigned notes = 3;
// A held key: its own line (journey.h) in the chosen scale.
struct Voice {
    Note note[notes];
    journey::Line line;
    opxy::u32 clock;      // output frames since the note started
    opxy::u32 next;       // when the next step is due
    opxy::u32 owner;
};
struct Shared { hwtone::Shared halcyon; };
}
