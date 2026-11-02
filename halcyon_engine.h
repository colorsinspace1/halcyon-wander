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
//   4 Hiss      bright tape hiss that soon breaks up into particles, which also flick the filter open
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
    v.soft = 0;
    v.bloom = 0;
    v.blocks = 0;
    v.up_level = 0;
    v.down_level = 0;
    v.hiss_low = 0;
    v.hiss_high = 0;
    v.dust = 0;
    v.dust_decay = 0;
    v.dust_low = 0;
    v.dust_random = 0x9e3779b9u ^ v.random;
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

    // --- High notes: kept soft ----------------------------------------------
    // Above about C5 (523 Hz) the tone's brightness stops climbing with the note:
    // the low-pass, the octave-up and the ninth stay where they were, and the
    // level eases down a little (about 5 dB two octaves higher), as on a piano.
    const u32 high_point = 50960000u;                                             // 523 Hz (C5)
    const u32 tilt = pitch0 <= high_point ? 32767u : (high_point >> 12) * 32767u / (pitch0 >> 12);  // Q15
    // --- Warmth: sine, and the soft saw through its low-pass ------------------
    const i32 sine_gain = q31(32767u - (warmth >> 1));
    const i32 saw_gain = q31(warmth * 24576u >> 15);                              // up to 0.75
    // The low-pass follows the note: 1.25x to 4.5x its pitch, opened by the bloom
    // and moved by the breath. Coefficient f = pi * ratio * increment, Q31.
    u32 ratio = 320u + (warmth * 832u >> 15);                                     // Q8
    ratio = ratio * opening >> 15;
    ratio = ratio * breath_scale >> 15;
    ratio = ratio * tilt >> 15;
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
        up_target = q31(((air >= 16384u ? 32767u : air << 1) * 9830u >> 15) * tilt >> 15);   // max 0.30
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
        fifth_target = q31(((chord >= 16384u ? 32767u : chord << 1) * 8192u >> 15) * ((32767u + tilt) >> 1) >> 15);  // max 0.25
    if (chord > 16384u && pitch0 < 0x38e38e38u)
        ninth_target = q31((((chord - 16384u) << 1) * 5898u >> 15) * tilt >> 15);   // max 0.18
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

    // --- Hiss: noise between about 2 and 10 kHz, then particles ---------------
    // Up to 20% it is a steady hiss; from 10% particles appear and quickly take
    // over: a few a second at first, about 200 a second at full, while the
    // steady hiss falls back to a whisper underneath.
    const u32 steady_hiss = hiss <= 6554u ? hiss * 5u : 32767u - ((hiss - 6554u) * 24575u / 26213u);  // Q15, peak at 20%
    const i32 hiss_gain = q31(steady_hiss * steady_hiss >> 15 >> 8);              // up to 0.004: quiet
    const u32 grains = hiss > 3277u ? (hiss - 3277u) * 32767u / 29490u : 0u;      // Q15, 0 at 10%
    const u32 dust_rate = grains ? 4u + (196u * (grains * grains >> 15) >> 15) : 0u;  // particles a second
    const u32 dust_chance = dust_rate * 97391u * opxy::block_frames;              // per block, of 2^32
    const i32 dust_gain = grains ? q31(131u + (grains * 262u >> 15)) : 0;         // 0.004 .. 0.012: quiet, under the tone
    // A particle may start at the top of a block; its level is worked out per
    // block (each fades over several) and ramped across the samples between.
    i32 dust_now = 0, dust_step = 0;
    if (dust_gain) {
        v.dust_random = v.dust_random * 1664525u + 1013904223u;
        if (v.dust_random < dust_chance) {
            const u32 pick = v.dust_random * 2891336453u + 1u;
            v.dust = static_cast<i32>(0x26666666u + ((pick * 747796405u) >> 2));         // 0.3 .. 0.8
            i32 d = 2131000000 + static_cast<i32>((pick >> 12) & 0x3fffu) * 600;           // fade per sample
            for (unsigned k = 0; k < 6; ++k) d = multiply(d, d);                           // ... and per block
            v.dust_decay = d;
        }
    }
    const bool dust_live = dust_gain && v.dust > 0x01000000;                       // let go below -42 dB
    if (dust_live) {
        dust_now = multiply(v.dust, dust_gain);
        v.dust = multiply(v.dust, v.dust_decay);
        dust_step = (multiply(v.dust, dust_gain) - dust_now) >> 6;
    } else {
        v.dust = 0;
    }
    const bool noisy = hiss_gain || dust_live;
    // A sounding particle nudges the saw's low-pass open a little, set
    // once a block from the particle's level (it fades over several blocks).
    i32 fi = f;
    if (dust_live) {
        const i32 bump = multiply(f, dust_now);
        fi = bump >= (coefficient_limit - f) >> 5 ? coefficient_limit : f + (bump << 5);
    }

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
    target = multiply(target, q31(16384u + (tilt >> 1)));                      // high notes a little softer
    const i32 ramp = (target - v.level) >> 6;
    i32 level = v.level;
    v.level = target;

    // The block is worked out in passes, one sound at a time, each a short loop
    // with nothing to decide inside it: the OP-XY's processor runs those fastest.
    const unsigned N = opxy::block_frames;
    u32 inc[N];
    {
        bool steady = true;
        for (unsigned i = 1; i < N; ++i) if (pitch[i] != pitch0) { steady = false; break; }
        if (steady) {
            const u32 held_increment = pitch0 + static_cast<u32>(multiply(static_cast<i32>(pitch0), bend));
            for (unsigned i = 0; i < N; ++i) inc[i] = held_increment;
        } else {
            for (unsigned i = 0; i < N; ++i) inc[i] = pitch[i] + static_cast<u32>(multiply(static_cast<i32>(pitch[i]), bend));
        }
    }
    i32 tone[N];
    // One band of noise, about 2 to 10 kHz, worked out every other sample and
    // held; the hiss and the particles both use it.
    i32 noise[N];
    if (noisy) {
        for (unsigned i = 0; i < N; i += 2) {
            v.random = v.random * 1664525u + 1013904223u;                         // one step of a cheap noise source
            const i32 white = static_cast<i32>(v.random) >> 2;                    // Q29
            v.hiss_high += (white - v.hiss_high) - ((white - v.hiss_high) >> 3);
            v.hiss_low += (v.hiss_high - v.hiss_low) >> 2;
            noise[i] = noise[i + 1] = (v.hiss_high - v.hiss_low) << 1;
        }
    }
    // Hiss's particles: half heard as they are, half into the saw's low-pass
    // (all heard straight when there is no saw).
    i32 grain[N];
    if (dust_live) for (unsigned i = 0; i < N; ++i) { dust_now += dust_step; grain[i] = multiply(noise[i], dust_now); }
    // The note: its sine, and its saw through the low-pass (12 dB an octave,
    // then a third pole: 18 dB). Subtracted: a rising saw's fundamental is
    // opposite the sine's.
    {
        u32 ph = v.phase;
        if (saw_gain) {
            i32 low = v.low, band = v.band, soft = v.soft;
            if (dust_live) {
                for (unsigned i = 0; i < N; ++i) {
                    const i32 saws = multiply((smooth_saw ? soft_saw(ph, inc[i], inverse) : static_cast<i32>(ph ^ 0x80000000u) >> 3) << 1, saw_now) + grain[i];
                    low += multiply(band, fi);
                    band += multiply(saws - low - band - (band >> 2), fi);       // damping 1.25
                    soft += multiply(low - soft, fi);
                    tone[i] = multiply(table_sine(ph) << 14, sine_now) - soft + (grain[i] >> 1);
                    ph += inc[i];
                }
            } else {
                for (unsigned i = 0; i < N; ++i) {
                    const i32 saws = multiply((smooth_saw ? soft_saw(ph, inc[i], inverse) : static_cast<i32>(ph ^ 0x80000000u) >> 3) << 1, saw_now);
                    low += multiply(band, fi);
                    band += multiply(saws - low - band - (band >> 2), fi);       // damping 1.25
                    soft += multiply(low - soft, fi);
                    tone[i] = multiply(table_sine(ph) << 14, sine_now) - soft;
                    ph += inc[i];
                }
            }
            v.low = low; v.band = band; v.soft = soft;
        } else {
            for (unsigned i = 0; i < N; ++i) { tone[i] = multiply(table_sine(ph) << 14, sine_now); ph += inc[i]; }
            if (dust_live) for (unsigned i = 0; i < N; ++i) tone[i] += grain[i];
        }
        v.phase = ph;
    }
    if (doubled) {
        // The ensemble's copy: a sine, slightly detuned.
        u32 cp = v.copy_phase;
        for (unsigned i = 0; i < N; ++i) {
            tone[i] += multiply(table_sine(cp) << 14, copy_now);
            cp += inc[i] + static_cast<u32>(multiply(static_cast<i32>(inc[i]), copy_detune));
        }
        v.copy_phase = cp;
    }
    if (layered) {
        u32 up = v.phase_up, down = v.phase_down;
        const i32 up_level = v.up_level, down_level = v.down_level;
        for (unsigned i = 0; i < N; ++i) {
            tone[i] += multiply(table_sine(up) << 14, up_level) + multiply(table_sine(down) << 14, down_level);
            up += (inc[i] << 1) + up_extra;
            down += (inc[i] >> 1) + down_extra;
        }
        v.phase_up = up; v.phase_down = down;
    }
    if (chorded) {
        u32 fifth = v.phase_fifth, ninth = v.phase_ninth, seventh = v.phase_seventh;
        const i32 fifth_level = v.fifth_level, ninth_level = v.ninth_level, seventh_level = v.seventh_level;
        for (unsigned i = 0; i < N; ++i) {
            tone[i] += multiply(table_sine(fifth) << 14, fifth_level) + multiply(table_sine(ninth) << 14, ninth_level)
                     + multiply(table_sine(seventh) << 14, seventh_level);
            fifth += inc[i] + (inc[i] >> 1);
            ninth += (inc[i] << 1) + (inc[i] >> 2);
            seventh += inc[i] + (inc[i] >> 1) + (inc[i] >> 2);
        }
        v.phase_fifth = fifth; v.phase_ninth = ninth; v.phase_seventh = seventh;
    }
    if (age) {
        i32 tape = v.tape;
        for (unsigned i = 0; i < N; ++i) {
            // Soft saturation: d - 4 d^3 / 27, which flattens smoothly to 1 at d = 1.5.
            i32 d = tone[i] + multiply(tone[i] << 1, drive);
            if (d > 0x30000000) d = 0x30000000;
            if (d < -0x30000000) d = -0x30000000;
            const i32 bend_down = multiply(multiply(d, d) << 2, 1272582900);     // 4/27 d^2, Q31
            const i32 saturated = multiply(multiply(d, full - bend_down), makeup);
            ease(tape, saturated, tape_rate);
            tone[i] += multiply(tape - tone[i], wet);
        }
        v.tape = tape;
    }
    if (hiss_gain) for (unsigned i = 0; i < N; ++i) tone[i] += multiply(noise[i], hiss_gain);
    for (unsigned i = 0; i < N; ++i) {
        i32 t = tone[i];
        // A soft ceiling: untouched up to the knee, then curving smoothly over.
        if (t > knee || t < -knee) {
            const bool down = t < 0;
            i32 e = (down ? -t : t) - knee;
            if (e > knee_top - knee) e = knee_top - knee;
            const i32 y = knee + e - (multiply(multiply(e, e) << 2, 0x2aaaaaab) << 2);   // e - e^2 / (4 * 0.1875)
            t = down ? -y : y;
        }
        level += ramp;
        output[i] = multiply(t << 1, level);
    }
}
}
