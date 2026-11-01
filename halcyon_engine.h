#include "halcyon_core.h"
#include "sine_table.h"
#include <opxy/q31.hpp>

// Halcyon: soft ambient tones after Aphex Twin's Rhubarb and Eno's An Ending (Ascent).
// Page 1:
//   0 Warmth    a pure sine; a soft saw through a gentle low-pass blends in (the CS-80 and Matrix pad)
//   1 Swell     a reverse-tape bloom from 10 ms to about 5 s; the filter opens with it
//   2 Drift     wandering tape speed, each voice slightly apart, and the filter's slow breath (off at 0);
//               it also sets the depth of six very slow built-in sines on the other knobs
//   3 Air       late copies an octave up, then an octave down (Ascent's delayed octaves)
// Page 2:
//   4 Hiss      soft tape hiss and air that swells with each note
//   5 Chord     adds a pure fifth, then a ninth, to every note
//   6 Ensemble  a second, slightly detuned copy that slowly beats against the first
//   7 Age       tape saturation, a rounded top and a little wobble in level
// Fixed point only: no floating point or 64-bit arithmetic; divisions once per block.
namespace hwtone {
namespace {
using opxy::i32;
using opxy::u32;
using opxy::q31::multiply;

inline u32 next_random(Voice& v) {
    u32 x = v.random ? v.random : 0x2545f491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v.random = x;
    return x;
}
inline i32 noise(Voice& v) { return static_cast<i32>(next_random(v)) >> 16; }  // Q15
inline u32 amount(i32 control) { return control <= 0 ? 0u : static_cast<u32>(control) >> 16; }  // 0..32767
inline i32 q31(u32 amount15) { return static_cast<i32>(amount15) << 16; }

// Sine of a phase in turns, Q15: 512-step table, linear interpolation.
inline i32 table_sine(u32 phase) {
    const u32 index = phase >> 23, fraction = (phase >> 8) & 0x7fffu;
    const i32 a = sine_table[index], b = sine_table[index + 1];
    return a + ((b - a) * static_cast<i32>(fraction) >> 15);
}

// A rising saw, Q28, with its jump rounded off (PolyBLEP) so it stays clean at any pitch.
inline i32 soft_saw(u32 phase, u32 increment, u32 inverse) {
    i32 saw = static_cast<i32>(phase ^ 0x80000000u) >> 3;
    if (phase < increment) {
        u32 x = (phase >> 8) * inverse >> 16;
        if (x > 32767u) x = 32767u;
        const u32 rest = 32768u - x;
        saw += static_cast<i32>(rest * rest >> 2);
    } else if (phase > 0xffffffffu - increment) {
        u32 y = ((phase + increment) >> 8) * inverse >> 16;
        if (y > 32767u) y = 32767u;
        saw -= static_cast<i32>(y * y >> 2);
    }
    return saw;
}

// Moves `value` a fraction `rate` of the way to `target`.
inline void ease(i32& value, i32 target, i32 rate) { value += multiply(target - value, rate); }

const u32 block_hertz = 6233086u;      // phase per block for 1 Hz: 2^32 * 64 / 44100
const i32 full = 0x7fffffff;
const i32 damping = 1342177280;        // the low-pass's damping 1.25, halved, Q31: no resonant peak
const i32 coefficient_limit = 0x59999999;  // 0.7: keeps the low-pass stable
const i32 output_gain = 0x2e147ae1;    // 0.36 of half scale: chords stay clean
const i32 knee = 0x20000000;           // 1.0 in Q29: above this the mix is rounded off gently...
const i32 knee_top = 0x2c000000;       // ...reaching its ceiling, 1.1875, at 1.375; never a hard clip
const i32 knob_rate = 0x04000000;      // page-2 levels settle in about 50 ms

// The built-in sways: a very slow sine on six knobs, deepest at full Drift.
// Depth is +- a share of the knob's range (Q15); each has its own period, so
// they never line up. They run on the voices' shared clock, so they keep moving
// from note to note; each voice sits a little apart in the cycle.
//                         Warmth   Air      Hiss     Chord    Ensemble Age
const u32 sway_depth[6] = {3277u,   1966u,   983u,    3277u,   3932u,   1311u};    // 10 6 3 10 12 4 %
const u32 sway_step[6]  = {328057u, 366652u, 271004u, 214934u, 239734u, 201067u}; // 19 17 23 29 26 31 s

// A knob moved by its sway; a knob at 0 stays at 0.
inline u32 swayed(const Voice& v, u32 clock, unsigned which, u32 value, u32 drift) {
    if (!value || !drift) return value;
    const i32 depth = static_cast<i32>(sway_depth[which] * drift >> 15);
    const u32 phase = clock * sway_step[which] + v.sway[which];
    const i32 moved = static_cast<i32>(value) + (table_sine(phase) * depth >> 15);
    return moved < 0 ? 0u : moved > 32767 ? 32767u : static_cast<u32>(moved);
}
}

inline void initialize(Voice& v, Shared&, opxy::u32 owner) {
    v = Voice();
    v.random = 0x9e3779b9u ^ (owner * 0x85ebca6bu);
    if (!v.random) v.random = 1;
    // Each voice starts its tape, breath and chorus somewhere different, so a
    // chord wavers against itself instead of moving in lockstep.
    v.detune = noise(v);
    v.capstan = next_random(v);
    v.chorus = next_random(v);
    v.reel = next_random(v);
    v.wander_target = noise(v);
    v.wander = v.wander_smooth = v.wander_target >> 1;
    v.breath_target = noise(v);
    v.breath = v.breath_smooth = v.breath_target >> 1;
    for (unsigned k = 0; k < 6; ++k) v.sway[k] = next_random(v) >> 3;   // up to 1/8 of a cycle apart
}

inline void start(Voice& v, Shared& shared, opxy::u32) {
    v.seen = shared.clock - 1u;        // a new note never moves the clock on its first block
    v.phase = 0;
    v.copy_phase = next_random(v);     // the copy starts elsewhere, so it beats from the first moment
    v.phase_up = 0;
    v.phase_down = 0;
    v.phase_fifth = 0;
    v.phase_ninth = 0;
    v.phase_seventh = 0;
    v.low = 0;
    v.band = 0;
    v.bloom = 0;
    v.blocks = 0;
    v.up_level = 0;
    v.down_level = 0;
    v.hiss_low = 0;
    v.hiss_high = 0;
    v.tape = 0;
    v.level = 0;
}

inline void destroy(Voice&, Shared&, opxy::u32) {}

inline void render(Voice& v, Shared& shared, opxy::u32, opxy::Output output,
                   opxy::Parameters parameters, opxy::Pitch pitch, i32 scale = 0x7fffffff) {
    // The shared clock: if no other voice has moved it since this voice last
    // looked, this voice is the first to render this block, so it moves it on.
    if (shared.clock == v.seen) ++shared.clock;
    v.seen = shared.clock;
    const u32 clock = shared.clock;
    const u32 drift = amount(parameters[2]);
    const u32 warmth = swayed(v, clock, 0, amount(parameters[0]), drift);
    const u32 swell = amount(parameters[1]);
    const u32 air = swayed(v, clock, 1, amount(parameters[3]), drift);
    const u32 hiss = swayed(v, clock, 2, amount(parameters[4]), drift);
    const u32 chord = swayed(v, clock, 3, amount(parameters[5]), drift);
    const u32 ensemble = swayed(v, clock, 4, amount(parameters[6]), drift);
    const u32 age = swayed(v, clock, 5, amount(parameters[7]), drift);
    const u32 pitch0 = pitch[0];
    if (v.blocks < 0xffffu) ++v.blocks;

    // --- Swell: a reverse-tape bloom ------------------------------------------
    // Its length runs from 10 ms to about 5 s; the level follows a reverb tail
    // played backwards: barely there, then rising faster and faster into the note.
    const u32 spread = swell * 9u, shift = spread >> 15, part = spread & 0x7fffu;
    const u32 length = (7u << shift) + ((7u << shift) * part >> 15);  // blocks
    const u32 step = 0x7fffffffu / length;
    v.bloom = v.bloom > 0x7fffffffu - step ? 0x7fffffffu : v.bloom + step;
    const u32 position = v.bloom >> 16;                                  // Q15
    const u32 squared = position * position >> 15, cubed = squared * position >> 15;
    const u32 shape = (cubed * 27853u + position * 4915u) >> 15;          // 0.85 p^3 + 0.15 p
    const u32 opening = 13107u + (position * 19660u >> 15);               // filter 0.4 -> 1.0 with the bloom

    // --- Drift: tape that wanders, and a filter that breathes ------------------
    i32 bend = 0, up_bend = 0, down_bend = 0;
    u32 breath_scale = 32768u;
    if (drift) {
        if (!v.wander_blocks) {
            v.wander_target = noise(v);
            v.wander_blocks = 200u + (next_random(v) >> 23);             // a new heading every 0.3 .. 1 s
        }
        --v.wander_blocks;
        v.wander += (v.wander_target - v.wander) >> 7;                   // two smoothing stages:
        v.wander_smooth += (v.wander - v.wander_smooth) >> 7;            // slow, noise-like, no corners
        v.capstan += block_hertz * 45u / 100u;                           // the capstan's slow 0.45 Hz turn
        i32 movement = (v.wander_smooth * 21 >> 5) + (table_sine(v.capstan) * 6 >> 5) + (v.detune * 5 >> 5);
        if (movement > 32767) movement = 32767;
        if (movement < -32767) movement = -32767;
        bend = (movement * static_cast<i32>(drift) >> 5) * 9 >> 3;     // up to about +-30 cents
        if (v.wow) bend = (bend >> 8) * v.wow;                            // deeper with the tone's wow
        up_bend = (bend >> 1) + static_cast<i32>(drift) * 45;            // the octaves waver on their own
        down_bend = -(bend >> 1) - static_cast<i32>(drift) * 30;
        if (!v.breath_blocks) {
            v.breath_target = noise(v);
            v.breath_blocks = 400u + (next_random(v) >> 22);             // every 0.6 .. 2 s
        }
        --v.breath_blocks;
        v.breath += (v.breath_target - v.breath) >> 8;
        v.breath_smooth += (v.breath - v.breath_smooth) >> 8;
        breath_scale = static_cast<u32>(32768 + ((v.breath_smooth * static_cast<i32>(drift) >> 15) * 18432 >> 15));
    }

    // --- Warmth: sine, and the soft saw through its low-pass ------------------
    const i32 sine_gain = q31(32767u - (warmth >> 1));
    const i32 saw_gain = q31(warmth * 24576u >> 15);                              // up to 0.75
    // The low-pass follows the note: 1.5x to 5.5x its pitch, opened by the bloom
    // and moved by the breath. Coefficient f = pi * ratio * increment, Q31.
    u32 ratio = 384u + (warmth * 1024u >> 15);                                    // Q8
    ratio = ratio * opening >> 15;
    ratio = ratio * breath_scale >> 15;
    u32 m = 804u * ratio >> 8;
    if (!m) m = 1;
    const u32 rate = pitch0 >> 8;
    const i32 f = rate >= static_cast<u32>(coefficient_limit) / m ? coefficient_limit : static_cast<i32>(rate * m);
    // The saws' corrections near their jump need 1/increment: one division a block.
    const bool smooth_saw = saw_gain && rate >= 2u;
    const u32 inverse = smooth_saw ? 0x7fffffffu / rate : 0u;

    // --- Ensemble: a second copy, detuned 4 to 14 cents and slowly sweeping ----
    ease(v.copy_level, q31(ensemble), knob_rate);
    const bool doubled = v.copy_level > 0x10000;
    i32 copy_detune = 0;
    i32 main_gain = full, copy_gain = 0;
    if (doubled) {
        v.chorus += block_hertz * 35u / 100u + (static_cast<u32>(v.detune + 32768) << 6);  // 0.35 .. 0.55 Hz, per voice
        const u32 sweep = static_cast<u32>(table_sine(v.chorus) + 32768) >> 1;           // 0 .. 32767
        const i32 depth = multiply(17437000, v.copy_level);                            // 14 cents, Q31
        copy_detune = multiply(depth, q31(9830u + (sweep * 22937u >> 15)));          // 0.3 .. 1.0 of it
        main_gain = full - multiply(v.copy_level, 0x33333333);                         // 1.0 -> 0.6
        copy_gain = multiply(v.copy_level, 0x4ccccccd);                                // 0 -> 0.6
    }
    // The copy is a sine only (Halcyon Keys' lighter ensemble): the saw stays
    // single and keeps the copy's share of level, so the blend sounds the same.
    const i32 copy_sine_gain = multiply(sine_gain, copy_gain);
    const i32 main_sine_gain = multiply(sine_gain, main_gain), main_saw_gain = multiply(saw_gain, main_gain + copy_gain);

    // --- Air: the late octaves -----------------------------------------------
    const u32 wait = 103u + (swell * 827u >> 15);                                 // 0.15 .. 1.35 s
    const u32 fade = 207u + (swell * 1034u >> 15);                                // 0.3 .. 1.8 s
    const i32 fade_rate = static_cast<i32>(0x7fffffffu / fade);
    i32 up_target = 0, down_target = 0;
    if (air && pitch0 < 0x3fffffffu && v.blocks > wait)
        up_target = q31((air >= 16384u ? 32767u : air << 1) * 9830u >> 15);       // max 0.30
    if (air > 16384u && v.blocks > wait * 3u / 2u)
        down_target = q31(((air - 16384u) << 1) * 9175u >> 15);                   // max 0.28
    ease(v.up_level, up_target, fade_rate);
    ease(v.down_level, down_target, fade_rate);
    const bool layered = v.up_level > 0x10000 || v.down_level > 0x10000;
    const u32 up_extra = layered ? static_cast<u32>(multiply(static_cast<i32>(pitch0 << 1), up_bend)) : 0u;
    const u32 down_extra = layered ? static_cast<u32>(multiply(static_cast<i32>(pitch0 >> 1), down_bend)) : 0u;

    // --- Chord: a pure fifth (3:2), then a ninth (9:4), with the note ----------
    i32 fifth_target = 0, ninth_target = 0;
    if (chord && pitch0 < 0x55555555u)
        fifth_target = q31((chord >= 16384u ? 32767u : chord << 1) * 8192u >> 15);  // max 0.25
    if (chord > 16384u && pitch0 < 0x38e38e38u)
        ninth_target = q31(((chord - 16384u) << 1) * 5898u >> 15);                // max 0.18
    ease(v.fifth_level, fifth_target, knob_rate);
    ease(v.ninth_level, ninth_target, knob_rate);
    ease(v.seventh_level, pitch0 < 0x49249249u ? v.seventh_target : 0, knob_rate);
    const bool chorded = v.fifth_level > 0x10000 || v.ninth_level > 0x10000 || v.seventh_level > 0x10000;

    // The added tones take part of their share from the note itself, so the
    // whole stays about as loud however many layers sound.
    const i32 added = v.up_level + v.down_level + v.fifth_level + v.ninth_level + v.seventh_level;
    const i32 base = full - multiply(added, 0x50000000);                           // 5/8 of the added share
    // The base share folded into the note's own gains: one product a sample fewer.
    const i32 sine_now = multiply(main_sine_gain, base), copy_now = multiply(copy_sine_gain, base), saw_now = multiply(main_saw_gain, base);

    // --- Hiss: noise between about 1 and 7 kHz ---------------------------------
    const i32 hiss_gain = q31(hiss * hiss >> 15 >> 2);                            // up to 0.25, gentle at first

    // --- Age: saturation, a rounded top, a little wobble in level -------------
    // Age comes in on a curve: barely there below 15, then growing.
    const u32 aged = age * age >> 15;
    const i32 drive = q31(aged);                                                  // the signal is driven 1x .. 3x
    const i32 makeup = static_cast<i32>((0x7fffffffu / (32768u + aged)) << 15);  // and brought back by 1 / (1 + age)
    const i32 wet = q31(aged);                                                    // the saturated tape fades in under the clean tone
    const i32 tape_rate = full - q31(aged * 21299u >> 15);                        // open .. about 3 kHz
    u32 wobble_scale = 32767u;
    if (age) {
        if (!v.wobble_blocks) {
            v.wobble_target = noise(v);
            v.wobble_blocks = 150u + (next_random(v) >> 24);
        }
        --v.wobble_blocks;
        v.wobble += (v.wobble_target - v.wobble) >> 6;
        v.reel += block_hertz * 7u / 10u;                                         // the reel's 0.7 Hz turn
        const u32 dip = static_cast<u32>(((table_sine(v.reel) + v.wobble) >> 1) + 32768) >> 1;  // 0 .. 32767
        wobble_scale = 32767u - ((dip * aged >> 15) * 1638u >> 15);                // up to 5% softer
    }

    // --- Level: the bloom, ramped smoothly across the block --------------------
    // `scale`: the caller's own level for this note, folded into the same ramp.
    i32 target = multiply(multiply(q31(shape), output_gain), scale);
    target = multiply(target, q31(wobble_scale));
    const i32 ramp = (target - v.level) >> 6;
    i32 level = v.level;
    v.level = target;

    i32 hiss_value = 0;
    // A held pitch (the usual case) needs its bent increment only once a block.
    bool steady = true;
    for (unsigned i = 1; i < opxy::block_frames; ++i) if (pitch[i] != pitch0) { steady = false; break; }
    const u32 held_increment = pitch0 + static_cast<u32>(multiply(static_cast<i32>(pitch0), bend));
    const u32 held_copy = doubled ? held_increment + static_cast<u32>(multiply(static_cast<i32>(held_increment), copy_detune)) : 0u;
    for (unsigned i = 0; i < opxy::block_frames; ++i) {
        const u32 p = pitch[i];
        const u32 increment = steady ? held_increment : p + static_cast<u32>(multiply(static_cast<i32>(p), bend));
        i32 tone = multiply(table_sine(v.phase) << 14, sine_now);           // Q29
        i32 saws = 0;
        u32 copy_increment = 0;
        if (doubled) {
            copy_increment = steady ? held_copy : increment + static_cast<u32>(multiply(static_cast<i32>(increment), copy_detune));
            tone += multiply(table_sine(v.copy_phase) << 14, copy_now);
        }
        if (saw_gain) {
            const i32 a = smooth_saw ? soft_saw(v.phase, increment, inverse) : static_cast<i32>(v.phase ^ 0x80000000u) >> 3;
            saws = multiply(a << 1, saw_now);
            // State-variable low-pass, 12 dB per octave; both saws share it.
            v.low += multiply(v.band, f);
            const i32 high = saws - v.low - (multiply(v.band, damping) << 1);
            v.band += multiply(high, f);
            // Subtracted: a rising saw's fundamental is opposite the sine's, and
            // the two must reinforce, not cancel.
            tone -= v.low;
        }
        if (layered) {
            tone += multiply(table_sine(v.phase_up) << 14, v.up_level);
            tone += multiply(table_sine(v.phase_down) << 14, v.down_level);
            v.phase_up += (increment << 1) + up_extra;
            v.phase_down += (increment >> 1) + down_extra;
        }
        if (chorded) {
            tone += multiply(table_sine(v.phase_fifth) << 14, v.fifth_level);
            tone += multiply(table_sine(v.phase_ninth) << 14, v.ninth_level);
            v.phase_fifth += increment + (increment >> 1);
            v.phase_ninth += (increment << 1) + (increment >> 2);
            tone += multiply(table_sine(v.phase_seventh) << 14, v.seventh_level);
            v.phase_seventh += increment + (increment >> 1) + (increment >> 2);
        }
        if (age) {
            // Soft saturation: d - 4 d^3 / 27, which flattens smoothly to 1 at d = 1.5.
            i32 d = tone + multiply(tone << 1, drive);
            if (d > 0x30000000) d = 0x30000000;
            if (d < -0x30000000) d = -0x30000000;
            const i32 bend_down = multiply(multiply(d, d) << 2, 1272582900);     // 4/27 d^2, Q31
            const i32 saturated = multiply(multiply(d, full - bend_down), makeup);
            ease(v.tape, saturated, tape_rate);
            tone += multiply(v.tape - tone, wet);
        }
        if (hiss_gain) {
            // The hiss runs at half rate: each new value is held for two samples.
            if (!(i & 1u)) {
                v.random = v.random * 1664525u + 1013904223u;                     // one step of a cheap noise source
                const i32 white = static_cast<i32>(v.random) >> 2;                // Q29
                const i32 rise = white - v.hiss_high;
                v.hiss_high += rise - (rise >> 2);                                // about 7 kHz down
                v.hiss_low += (v.hiss_high - v.hiss_low) >> 2;                    // about 1 kHz up
                hiss_value = multiply((v.hiss_high - v.hiss_low) << 1, hiss_gain);
            }
            tone += hiss_value;
        }
        // A soft ceiling: untouched up to the knee, then curving smoothly over.
        if (tone > knee || tone < -knee) {
            const bool down = tone < 0;
            i32 e = (down ? -tone : tone) - knee;
            if (e > knee_top - knee) e = knee_top - knee;
            const i32 y = knee + e - (multiply(multiply(e, e) << 2, 0x2aaaaaab) << 2);   // e - e^2 / (4 * 0.1875)
            tone = down ? -y : y;
        }
        level += ramp;
        output[i] = multiply(tone << 1, level);
        v.phase += increment;
        if (doubled) v.copy_phase += copy_increment;
    }
}
}
