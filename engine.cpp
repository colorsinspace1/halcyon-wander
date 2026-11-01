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

// The tones: Colors In Space's fifteen Halcyon sounds (CIS 1-15), each Halcyon's
// eight control words (Warmth, Swell, Drift, Air, Hiss, Chord, Ensemble, Age)
// as he set them, and how fast each note fades, per block, from his amp
// envelope (release, or decay where sustain is 0). New tones go at the end:
// presets store the index.
struct Tone { i32 controls[8]; i32 fade_step; i32 seventh; i32 wow; };   // seventh Q31, wow Q8
const Tone tones[journey::tone_count] = {
    {{2147483647, 0, 0, 85852160, 42926080, 0, 0, 0}, 2136227646, 0, 256},   // CIS 1, 1.9 s
    {{0, 0, 0, 0, 0, 0, 493879296, 407961600}, 2143677633, 0, 256},   // CIS 2, 5.7 s
    {{2147483647, 0, 0, 322043904, 472383488, 107347968, 644218880, 107347968}, 2136227646, 0, 256},   // CIS 3, 1.9 s
    {{2147483647, 0, 0, 2147483647, 279052288, 214695936, 21430272, 2147483647}, 2137806203, 0, 256},   // CIS 4, 2.2 s
    {{923336704, 0, 622723072, 579796992, 171769856, 171769856, 751566848, 751566848}, 2138468278, 0, 256},   // CIS 5, 2.4 s
    {{880082944, 217645056, 1061158912, 1419378688, 493879296, 558301184, 2147483647, 1202520064}, 2140308230, 0, 256},   // CIS 6, 3.0 s
    {{1567621120, 1202520064, 0, 2147483647, 128843776, 515375104, 1460273152, 386531328}, 2139337091, 0, 256},   // CIS 7, 2.6 s
    {{2147483647, 171769856, 730071040, 944898048, 858980352, 171769856, 42926080, 730071040}, 2139337091, 0, 256},   // CIS 8, 2.6 s
    {{429457408, 0, 644218880, 365035520, 880476160, 0, 0, 1073741824}, 2143900909, 0, 256},   // CIS 9, 6.0 s
    {{1288503296, 622723072, 343539712, 730071040, 407961600, 1095172096, 407961600, 279117824}, 2134761059, 0, 256},   // CIS 10, 1.7 s
    {{2147483647, 858980352, 0, 2147483647, 343539712, 0, 0, 0}, 2139845361, 0, 256},   // CIS 11, 2.8 s
    {{214695936, 0, 0, 1309933568, 386531328, 558301184, 0, 1632043008}, 2142850726, 0, 256},   // CIS 12, 4.6 s
    {{429457408, 1288437760, 1009319936, 0, 0, 407961600, 0, 0}, 2136862140, 0, 256},   // CIS 13, 2.0 s
    {{2147483647, 1266941952, 536870912, 407961600, 450953216, 21430272, 1138163712, 858980352}, 2140080119, 0, 256},   // CIS 14, 2.9 s
    {{1266941952, 0, 0, 0, 0, 0, 0, 0}, 2138920536, 0, 256},   // CIS 15, 2.5 s
};

const i32 silent = 0x00b8449c;      // below this a note is let go (-45 dB)
const i32 mix = 0x35c28f5c;         // each note at 0.42: a little louder than before, overlapping notes stay clean
// The last stage: peaks above 0.45 are rounded off softly towards a ceiling
// of 0.6, never clipped; ordinary playing stays below the knee, untouched.
// Each sound's loudness, matched by ear-level RMS (about -21 dBFS) with peaks
// kept under the soft knee: a Q31 fraction and a left shift (+7 to +26 dB).
struct Lift { u32 shift; i32 fraction; };
const Lift lifts[journey::tone_count] = {
    {2u, 1971831836},
    {2u, 1364174334},
    {4u, 1190720643},
    {4u, 1516386408},
    {3u, 1814846418},
    {3u, 1617483573},
    {5u, 1338997873},
    {3u, 2098166161},
    {2u, 1273121564},
    {4u, 1551707585},
    {3u, 1119025230},
    {2u, 1971831836},
    {4u, 2008213503},
    {5u, 1243884016},
    {2u, 1994664573}
};
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
    const Lift lift = lifts[c.sound];
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        i32 x = multiply(output[i], lift.fraction);
        const i32 limit = 0x7fffffff >> lift.shift;              // saturate instead of wrapping
        if (x > limit) x = limit;
        if (x < -limit) x = -limit;
        output[i] = soften(x << lift.shift);
    }
    v.clock = now + opxy::block_frames;
}
}
