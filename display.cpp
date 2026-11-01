#include "display.h"
#include "panel.h"

// Halcyon Wander's screen: an orrery in greys. The held key is the sun; each ring is an
// octave of Spread; the scale's notes light up at their clock positions. A marble
// lands on each note as it sounds, its last moves trailing behind. It follows the line itself (journey.h), in step with the sound.
namespace hwander {
namespace {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

void note_place(u32 degree, u32 scale, u32 octaves, i32& x, i32& y) {
    const u32 size = journey::scale_size[scale];
    u32 ring = degree / size;
    if (ring >= octaves) ring = octaves - 1u;   // a note from a wider Spread sits on the outer ring
    const u32 semitone = journey::scale_notes[scale][degree % size];
    x = panel::place_x[ring][semitone];
    y = panel::place_y[ring][semitone];
}
}

inline void display_reset(DisplayState& state) {
    // A new visit to the page: the header starts over; a held line carries on.
    state.header.reset();
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    const journey::Controls c = journey::controls(input.slots);

    // The header: Sound, Rate, Chance, Scale; page 2 Deja vu, Length, Spread, Jitter.
    const char* const titles[8] = {"Sound", "Rate", "Chance", "Scale", "Deja vu", "Length", "Spread", "Jitter"};
    const char* const scales[8] = {"min", "dor", "harm", "phry", "lyd", "maj", "Mpen", "mpen"};
    char values[8][header_capacity] = {};
    for (u32 i = 0; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    const char* const tones[journey::tone_count] = {"CIS01", "CIS02", "CIS03", "CIS04", "CIS05", "CIS06", "CIS07", "CIS08", "CIS09", "CIS10", "CIS11", "CIS12", "CIS13", "CIS14", "CIS15"};
    copy(values[0], header_capacity, tones[c.sound]);
    copy(values[3], header_capacity, scales[c.scale]);
    values[5][0] = c.length >= 10 ? '1' : static_cast<char>('0' + c.length);
    values[5][1] = c.length >= 10 ? static_cast<char>('0' + c.length - 10) : 0;
    values[5][2] = 0;
    copy(values[6], header_capacity, "1 oct");
    values[6][0] = static_cast<char>('0' + c.octaves);
    page_header(state.header, input, frame, titles, values, panel::bar0);

    // The line: started when a key goes down, stepped on the sound's clock.
    // A note start after a quiet second restarts the line (the OP-XY's note-start
    // count). Starts closer together, as a held sequence sends, leave it wandering.
    // The OP-XY's held-key count can't be trusted (it reads 0 while keys are held,
    // and flicks to 1 and back on a press), so releases are never acted on: once
    // started, the line wanders on until the next note restarts it.
    const NoteEvents events = state.notes.update(input);
    const bool fresh = state.serial_known && input.note_serial != state.serial;
    state.serial = input.note_serial;
    state.serial_known = 1;
    const i32 dt = clamp(input.elapsed, 0, 1024);
    const u32 frames = static_cast<u32>(dt) * 44100u >> 10;
    const bool restart = events.hold_started || (fresh && (!state.playing || state.quiet >= 44100u));
    if (fresh) state.quiet = 0;
    else if (state.quiet < 0x10000000u) state.quiet += frames;
    if (restart) {
        journey::begin(state.line);
        state.playing = 1;
        state.time = 0;
        state.next = 0;
        state.trail_count = 0;
        state.seen = 0;
    }
    const u32 step_frames = journey::interval(c.rate);
    if (state.playing) {
        state.time += frames;
        while (state.time >= journey::when(state.line, c, state.next, step_frames)) {
            const u32 at = journey::when(state.line, c, state.next, step_frames);
            state.next += step_frames;
            const journey::Step s = journey::next(state.line, c);
            if (!s.sounds) continue;
            for (u32 k = 4; k > 0; --k) state.trail[k] = state.trail[k - 1];
            state.trail[0] = s.degree;
            if (state.trail_count < 5) ++state.trail_count;
            state.sounded = at;
            state.gain = s.gain;
        }
    } else {
        state.seen += frames;
    }
    const bool shown = state.playing || state.seen < 44100u;   // fades for a second after release

    // Rings for the octaves in reach, and the scale's places on them.
    for (u32 ring = 0; ring < 4; ++ring) {
        if (ring < c.octaves) place(frame, panel::ring0 + ring, panel::centre_x * unit, panel::centre_y * unit, unit, panel::shade_ring, stroke);
        else hide(frame, panel::ring0 + ring);
        for (u32 s = 0; s < 12; ++s) {
            const u32 n = panel::dot0_0 + ring * 12 + s;
            if (ring >= c.octaves) { hide(frame, n); continue; }
            bool lit = false;
            for (u32 k = 0; k < journey::scale_size[c.scale]; ++k) lit = lit || journey::scale_notes[c.scale][k] == s;
            place(frame, n, panel::place_x[ring][s], panel::place_y[ring][s], lit ? unit : unit / 2,
                  lit ? panel::shade_lit : panel::shade_dim, stroke);
        }
    }

    // The marble and its trail.
    const u32 since = state.time >= state.sounded ? state.time - state.sounded : 0;
    for (u32 k = 0; k < 5; ++k) {
        const u32 n = k == 0 ? panel::marble : panel::trail0 + (k - 1);
        if (!shown || k >= state.trail_count) { hide(frame, n); continue; }
        i32 x, y;
        note_place(state.trail[k], c.scale, c.octaves, x, y);
        // The marble sets off 0.15 s early and lands on the next note as it sounds:
        // the line is known ahead, so it peeks at the coming step.
        if (k == 0 && state.playing) {
            journey::Line ahead = state.line;
            const u32 due = journey::when(state.line, c, state.next, step_frames);
            const journey::Step coming = journey::next(ahead, c);
            if (coming.sounds && due > state.time && due - state.time < 6615u) {
                i32 tx, ty;
                note_place(coming.degree, c.scale, c.octaves, tx, ty);
                const i32 t = 256 - static_cast<i32>(due - state.time) * 256 / 6615;   // 0..255
                x = x + ((tx - x) / 256) * t;
                y = y + ((ty - y) / 256) * t;
            }
        }
        const u32 shade = k == 0 ? panel::shade_grey0 : panel::shade_grey0 + k;
        place(frame, n, x, y, unit, shade > panel::shade_grey4 ? panel::shade_grey4 : shade, fill_and_stroke);
    }
    // The sun's halo swells for a moment as each note sounds.
    const i32 swell = since < 8820u ? static_cast<i32>(8820u - since) * 600 / 8820 : 0;   // 0.2 s
    place(frame, panel::halo, panel::centre_x * unit, panel::centre_y * unit, unit + swell, panel::shade_grey2, stroke);

    // The sun brightens and grows while a key is held.
    place(frame, panel::sun, panel::centre_x * unit, panel::centre_y * unit, state.playing ? unit + unit / 4 : unit,
          panel::shade_grey0, fill_and_stroke);
    for (u32 k = 0; k < 3; ++k) hide(frame, panel::ripple0 + k);

}
}
