#pragma once
#include <opxy/types.hpp>

// The line a held key follows, shared by the sound (engine.cpp) and the screen
// (display.cpp): the same "random" recipe from the same start, on the same clock,
// so the screen shows the notes as they sound without the two ever talking.
// Like Mutable Instruments' Marbles: Deja vu loops the last Length steps, from
// always new (0) to a locked phrase (full).
namespace hwander { namespace journey {
typedef opxy::u32 u32;
typedef opxy::i32 i32;
const u32 loop_slots = 16;
const u32 tone_count = 15;     // the tones (engine.cpp)
const u32 scale_count = 8;
const u32 scale_size[scale_count] = {7, 7, 7, 7, 7, 7, 5, 5};
const u32 scale_notes[scale_count][7] = {
    {0, 2, 3, 5, 7, 8, 10},   // minor
    {0, 2, 3, 5, 7, 9, 10},   // dorian
    {0, 2, 3, 5, 7, 8, 11},   // harmonic minor
    {0, 1, 3, 5, 7, 8, 10},   // phrygian
    {0, 2, 4, 6, 7, 9, 11},   // lydian
    {0, 2, 4, 5, 7, 9, 11},   // major
    {0, 2, 4, 7, 9, 0, 0},    // major pentatonic (five notes)
    {0, 3, 5, 7, 10, 0, 0},   // minor pentatonic (five notes)
};

inline u32 choice(i32 v, u32 count) { return v <= 0 ? 0u : ((static_cast<u32>(v) >> 16) * count) >> 15; }
inline u32 amount(i32 v) { return v <= 0 ? 0u : static_cast<u32>(v) >> 16; }   // 0..32767

// (a * b) >> shift for shift 16..32, in 32-bit pieces: the device has no 64-bit
// multiply helper, so no unsigned long long anywhere.
inline u32 mul_shift(u32 a, u32 b, u32 shift) {
    const u32 al = a & 0xffffu, ah = a >> 16, bl = b & 0xffffu, bh = b >> 16;
    const u32 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    const u32 mid = (ll >> 16) + (lh & 0xffffu) + (hl & 0xffffu);
    const u32 hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
    const u32 lo = (mid << 16) | (ll & 0xffffu);
    return shift >= 32 ? hi >> (shift - 32) : (hi << (32 - shift)) | (lo >> shift);
}

inline u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

// Rate: from one step every 4 s to 20 a second, along a curve; output frames.
inline u32 interval(u32 rate) {
    const u32 octaves = rate * 6u;                      // Q15: up to 6 octaves faster
    const u32 whole = octaves >> 15, part = octaves & 32767u;
    u32 frames = 176400u >> whole;
    frames -= mul_shift(frames, part, 16);
    return frames < 2205u ? 2205u : frames;
}

// The controls the line reads, from the eight parameter words.
struct Controls { u32 sound, rate, chance, scale, dejavu, length, octaves, jitter; };
inline Controls controls(const i32* p) {
    Controls c;
    c.sound = choice(p[0], tone_count);
    if (c.sound >= tone_count) c.sound = tone_count - 1;
    c.rate = amount(p[1]);
    c.chance = amount(p[2]);
    c.scale = choice(p[3], scale_count);
    c.dejavu = amount(p[4]);
    c.length = 1u + choice(p[5], loop_slots);            // 1..16
    if (c.length > loop_slots) c.length = loop_slots;
    c.octaves = 1u + choice(p[6], 4);                    // 1..4
    if (c.octaves > 4) c.octaves = 4;
    c.jitter = amount(p[7]);
    return c;
}

struct Line { u32 steps; u32 loop[loop_slots]; };
struct Step { bool sounds; u32 degree; i32 gain; };     // degree 0 is the held key

inline void begin(Line& line) {
    line.steps = 0;
    for (u32 i = 0; i < loop_slots; ++i) line.loop[i] = 0;
}

// The next step. The first is always the held key.
inline Step next(Line& line, const Controls& c) {
    const u32 n = line.steps++;
    Step s = {true, 0, 32767};
    const u32 seed = 0x5bd1e995u * (c.sound + 1u);
    const u32 slot = n % c.length;
    u32 value;
    const bool again = n >= c.length && (hash(seed ^ (n * 0x9e3779b9u) ^ 0xd5u) >> 17) < c.dejavu;
    if (again) value = line.loop[slot];
    else { value = hash(seed + n * 0x85ebca6bu + 1u); line.loop[slot] = value; }
    if (n == 0) { line.loop[0] = 0; return s; }          // the held key, sounding
    s.sounds = (value & 32767u) < c.chance;
    s.degree = ((value >> 16) * (scale_size[c.scale] * c.octaves)) >> 16;  // anywhere in the range
    s.gain = static_cast<i32>(32767u - (((value >> 8) & 255u) * 32u));   // up to a quarter quieter
    return s;
}
// Jitter: when the line's next step lands, given when it is due on the beat
// (next): up to a quarter step early or late, the same for sound and screen.
// The held key's first note is always on time.
inline u32 when(const Line& line, const Controls& c, u32 next, u32 step_frames) {
    if (!c.jitter || !line.steps) return next;
    const u32 r = hash(0x2c1b3c6du ^ (line.steps * 0x9e3779b9u) ^ (c.sound * 0x85ebca6bu)) >> 17;   // 0..32767
    const u32 swing = ((step_frames >> 1) * c.jitter) >> 15;          // the whole window
    return next - (swing >> 1) + (((step_frames >> 1) * ((r * c.jitter) >> 15)) >> 15);
}
}}
