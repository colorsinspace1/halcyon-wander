#include "core.h"
#include "halcyon_engine.h"
#include <opxy/q31.hpp>

// Halcyon Wander: Wander's line (journey.h) playing Halcyon's tones. Hold a key
// and it plays a line of its own in the chosen scale, from the key you hold,
// after Mutable Instruments' Marbles: Rate sets how often it steps, Chance how
// many of those steps sound, Deja vu and Length how much it repeats itself,
// Spread how far it roams, Jitter loosens its timing. Sound picks a tone
// (pads, keys, plucks; tones below); each note is a whole Halcyon voice at its
// own pitch, swelling in as Halcyon does and fading at the tone's own pace.
namespace hwander {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::q31::multiply;

// 2^(n/12) for n = 0..48, Q16.
const u32 ratio[49] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715,
    131072, 138866, 147123, 155872, 165140, 174960, 185364, 196386, 208064, 220436, 233544, 247431,
    262144, 277732, 294247, 311744, 330281, 349920, 370728, 392772, 416128, 440872, 467088, 494862,
    524288, 555464, 588493, 623487, 660561, 699841, 741455, 785544, 832255, 881744, 934175, 989724,
    1048576};

// The tones: Halcyon's eight control words (Warmth, Swell, Drift, Air, Hiss,
// Chord, Ensemble, Age) and how fast each note fades, per block. Rhubarb is Halcyon's
// own preset (Swell at 0.6, so wandering notes bloom in time); Pylon and Warp
// wobble deeper than Halcyon's Drift goes. A tone may also add a harmonic
// seventh (7:4). New tones go at the end: presets store the index. New tones go at the end: presets store the index.
struct Tone { i32 controls[8]; i32 fade_step; i32 seventh; i32 wow; };   // seventh Q31, wow Q8
const Tone tones[journey::tone_count] = {
    {{1610612735, 708669604, 966367641, 322122547, 751619276, 0, 858993459, 1073741824}, 2142704923, 0, 256},   // Rhubarb, 4.5 s
    {{1717986918, 1073741824, 966367641, 322122547, 644245094, 0, 1181116006, 966367641}, 2144410389, 0, 256},   // Velvet, 7.0 s
    {{1503238553, 966367641, 751619276, 214748365, 536870912, 0, 966367641, 1181116006}, 2144410389, 0, 256},   // Amber, 7.0 s
    {{1395864371, 1073741824, 1932735282, 644245094, 1073741824, 0, 0, 1825361100}, 2144794305, 0, 512},   // Pylon, 8.0 s
    {{644245094, 1717986918, 644245094, 1932735282, 644245094, 536870912, 0, 429496729}, 2145092955, 0, 256},   // Tide, 9.0 s
    {{107374182, 1073741824, 858993459, 1288490188, 1503238553, 0, 0, 644245094}, 2143898607, 0, 256},   // Ghost, 6.0 s
    {{1503238553, 644245094, 1825361100, 214748365, 751619276, 429496729, 0, 1610612735}, 2143182317, 0, 512},   // Warp, 5.0 s
    {{644245094, 214748365, 322122547, 429496729, 107374182, 0, 751619276, 536870912}, 2140319550, 0, 256},   // Tine, 3.0 s
    {{966367641, 171798692, 322122547, 214748365, 107374182, 0, 644245094, 966367641}, 2138889601, 0, 256},   // Wurli, 2.5 s
    {{429496729, 85899346, 214748365, 644245094, 0, 536870912, 322122547, 429496729}, 2135556767, 0, 256},   // Mallet, 1.8 s
    {{751619276, 257698038, 429496729, 536870912, 214748365, 0, 858993459, 644245094}, 2140319550, 0, 256},   // Ivory, 3.0 s
    {{1073741824, 214748365, 751619276, 322122547, 858993459, 0, 751619276, 1610612735}, 2140319550, 0, 256},   // Cassette, 3.0 s
    {{966367641, 0, 214748365, 429496729, 0, 0, 536870912, 429496729}, 2129618190, 0, 256},   // Harp, 1.2 s
    {{536870912, 0, 107374182, 858993459, 0, 536870912, 0, 322122547}, 2123696127, 0, 256},   // Kalimba, 0.9 s
    {{429496729, 0, 107374182, 751619276, 0, 429496729, 107374182, 214748365}, 2111901359, 0, 256},   // Pebble, 0.6 s
};

const i32 silent = 0x00b8449c;      // below this a note is let go (-45 dB)
const i32 mix = 0x35c28f5c;         // each note at 0.42: a little louder than before, overlapping notes stay clean
// The last stage: peaks above 0.45 are rounded off softly towards a ceiling
// of 0.6, never clipped; ordinary playing stays below the knee, untouched.
inline i32 soften(i32 sum) {
    const u32 knee = 0x39999999u, room = 0x0ccccccdu;                // 0.45, and 0.15 more
    const bool down = sum < 0;
    const u32 a = down ? static_cast<u32>(-(sum + 1)) : static_cast<u32>(sum);
    if (a <= knee) return sum;
    const u32 E = (a - knee) >> 16, D = room >> 16;                   // 32-bit: no 64-bit helpers
    const i32 y = static_cast<i32>(knee + ((E * D / (E + D)) << 16));
    return down ? -y : y;
}

const u32 dying_blocks = 4;          // a given-way note fades out over about 6 ms

void begin_note(Note& note, Shared& shared, u32 owner, u32 note_ratio, i32 gain) {
    hwtone::start(note.tone, shared.halcyon, owner);
    note.ratio = note_ratio;
    note.gain = gain;
    note.fade = 0x7fffffff;
    note.dying = 0;
}

void sound_note(Voice& v, Shared& shared, u32 semitones, i32 gain) {
    const u32 note_ratio = ratio[semitones > 48 ? 48 : semitones];
    // A free note, or else the faintest, gives way.
    u32 pick = 0;
    i32 faintest = 0x7fffffff;
    for (u32 n = 0; n < notes; ++n) {
        if (!v.note[n].ratio) { pick = n; break; }
        const i32 f = v.note[n].dying ? 0 : v.note[n].fade;   // one already giving way goes first
        if (f < faintest) { faintest = f; pick = n; }
    }
    Note& note = v.note[pick];
    if (!note.ratio) { begin_note(note, shared, v.owner, note_ratio, gain); return; }
    // Still sounding: it fades out first, then this note starts.
    if (!note.dying) note.dying = dying_blocks;
    note.next_ratio = note_ratio;
    note.next_gain = gain;
}
}

inline void initialize(Voice& voice, Shared& shared, u32 owner) {
    voice = Voice();
    voice.owner = owner;
    for (u32 n = 0; n < notes; ++n) hwtone::initialize(voice.note[n].tone, shared.halcyon, owner * notes + n);
}

inline void start(Voice& voice, Shared&, u32) {
    for (u32 n = 0; n < notes; ++n) voice.note[n].ratio = 0;
    voice.clock = 0;
    voice.next = 0;
    journey::begin(voice.line);
}

inline void destroy(Voice&, Shared&, u32) {}

inline void render(Voice& v, Shared& shared, u32, opxy::Output output, opxy::Parameters p, opxy::Pitch pitch) {
    const journey::Controls c = journey::controls(p);
    const u32 step_frames = journey::interval(c.rate);

    // --- Steps, on the line's clock, once a block --------------------------------
    const u32 now = v.clock;
    while (now >= journey::when(v.line, c, v.next, step_frames)) {
        v.next += step_frames;
        const journey::Step s = journey::next(v.line, c);
        if (!s.sounds) continue;
        const u32 size = journey::scale_size[c.scale];
        sound_note(v, shared, journey::scale_notes[c.scale][s.degree % size] + 12u * (s.degree / size), s.gain);
    }

    // --- Sound: each note a Halcyon voice at its own pitch -----------------------
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = 0;
    i32 tone[opxy::block_frames];
    u32 raised[opxy::block_frames];
    bool steady = true;
    for (unsigned i = 1; i < opxy::block_frames; ++i) if (pitch[i] != pitch[0]) { steady = false; break; }
    for (u32 n = 0; n < notes; ++n) {
        Note& note = v.note[n];
        if (!note.ratio) continue;
        if (steady) {
            // The usual case, a held pitch: one product for the whole block.
            const u32 r = journey::mul_shift(pitch[0], note.ratio, 16);
            const u32 q = r > 0x3fffffffu ? 0x3fffffffu : r;               // at most a quarter of the sample rate
            for (unsigned i = 0; i < opxy::block_frames; ++i) raised[i] = q;
        } else {
            for (unsigned i = 0; i < opxy::block_frames; ++i) {
                const u32 r = journey::mul_shift(pitch[i], note.ratio, 16);
                raised[i] = r > 0x3fffffffu ? 0x3fffffffu : r;
            }
        }
        note.tone.seventh_target = tones[c.sound].seventh;
        note.tone.wow = tones[c.sound].wow;
        // The note's level for the end of this block; Halcyon glides to it across
        // the block inside its own level ramp, so there are no steps.
        if (note.dying) {
            --note.dying;
            note.fade = note.dying ? note.fade / static_cast<i32>(note.dying + 1) * static_cast<i32>(note.dying) : 0;
        } else {
            note.fade = multiply(note.fade, tones[c.sound].fade_step);
        }
        const i32 after = multiply(multiply(note.fade, static_cast<i32>(note.gain) << 16), mix);
        hwtone::render(note.tone, shared.halcyon, v.owner, tone, tones[c.sound].controls, raised, after);
        for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] += tone[i];
        if (!note.fade && note.next_ratio) {
            begin_note(note, shared, v.owner, note.next_ratio, note.next_gain);
            note.next_ratio = 0;
        } else if (note.fade < silent && !note.dying) {
            note.ratio = 0;
        }
    }
    for (unsigned i = 0; i < opxy::block_frames; ++i) output[i] = soften(output[i]);
    v.clock = now + opxy::block_frames;
}
}
