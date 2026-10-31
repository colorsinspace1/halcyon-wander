#pragma once
#include <opxy/page_header.hpp>
#include "journey.h"

namespace hwander {
// Matches resources.display in definition.json: paths, points, nodes, texts,
// palette. tools/make_panel.py writes the artwork (panel.h).
using Frame = opxy::draw::Frame<0, 0, 70, 16, 9>;
// The screen follows the held key's line itself (journey.h): the same steps at
// the same times as the sound, without hearing it.
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    journey::Line line;
    opxy::u32 playing;        // a key is held
    opxy::u32 time;           // output frames since it was pressed
    opxy::u32 next;           // when the next step is due
    opxy::u32 sounded;        // when the last note sounded
    opxy::i32 gain;           // its loudness, Q15
    opxy::u32 trail[5];       // the last notes' degrees, newest first
    opxy::u32 trail_count;
    opxy::u32 seen;           // frames since release, for the fade
    opxy::u32 serial;         // last note-start serial, a second way to see a key go down
    opxy::u32 serial_known;   // serial holds a real baseline
    opxy::u32 quiet;          // frames since the last note start
};
}
