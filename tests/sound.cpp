// Halcyon Wander's own sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>.
#include "../engine.cpp"
#include <cstdio>

namespace {
struct Result { bool sounding, bounded; unsigned notes; };
// Holds one key (about C4) for `blocks`, counting the notes begun.
Result hold(const opxy::i32 (&p)[8], unsigned blocks) {
    static hwander::Voice voice;
    static hwander::Shared shared;
    opxy::u32 pitch[opxy::block_frames];
    for (unsigned i = 0; i < opxy::block_frames; ++i) pitch[i] = 25480000u;
    opxy::i32 output[opxy::block_frames];
    hwander::initialize(voice, shared, 7);
    hwander::start(voice, shared, 7);
    Result r = {false, true, 0};
    for (unsigned b = 0; b < blocks; ++b) {
        hwander::render(voice, shared, 7, output, p, pitch);
        for (unsigned n = 0; n < hwander::notes; ++n)
            if (voice.note[n].ratio && voice.note[n].fade > 2142000000) ++r.notes;   // begun this block
        for (unsigned i = 0; i < opxy::block_frames; ++i) {
            r.sounding = r.sounding || output[i] != 0;
            r.bounded = r.bounded && output[i] >= -1288490189 && output[i] <= 1288490189;
        }
    }
    return r;
}
opxy::i32 pick(unsigned index, unsigned count) { return static_cast<opxy::i32>((index * 2 + 1) * (0x7fffffffu / (count * 2))); }
}

int main() {
    bool ok = true;
    const opxy::i32 hi = 0x7fffffff;
    // Every tone, every scale, slow and fast, plays and stays within the declared peak.
    for (unsigned sound = 0; sound < 5; ++sound)
        for (unsigned scale = 0; scale < 8; ++scale) {
            const opxy::i32 p[8] = {pick(sound, 5), scale % 2 ? hi : 0x30000000, hi, pick(scale, 8), 0x40000000, pick(7, 16), pick(3, 4), hi};
            const Result r = hold(p, 690);
            if (!r.sounding) { std::printf("FAIL silent (tone %u, scale %u)\n", sound + 1, scale); ok = false; }
            if (!r.bounded) { std::printf("FAIL above the declared peak (tone %u, scale %u)\n", sound + 1, scale); ok = false; }
        }
    // Chance at 0: only the held key's first note; at full and fast, many notes.
    {
        const opxy::i32 none[8] = {0, hi, 0, 0, 0, pick(7, 16), pick(1, 4), 0};
        const opxy::i32 all[8] = {0, hi, hi, 0, 0, pick(7, 16), pick(1, 4), 0};
        const Result a = hold(none, 1380), b = hold(all, 1380);
        if (a.notes != 1) { std::printf("FAIL Chance 0 plays %u notes, not 1\n", a.notes); ok = false; }
        if (b.notes < 20) { std::printf("FAIL Chance full plays only %u notes in 2 s\n", b.notes); ok = false; }
    }
    // Deja vu full, Length 4: after the first four steps the phrase repeats exactly.
    {
        const opxy::i32 p[8] = {0, hi, hi, 0, hi, pick(3, 16), pick(3, 4), 0};
        const hwander::journey::Controls c = hwander::journey::controls(p);
        hwander::journey::Line line;
        hwander::journey::begin(line);
        unsigned degree[24];
        for (unsigned n = 0; n < 24; ++n) degree[n] = hwander::journey::next(line, c).degree;
        for (unsigned n = 8; n < 24; ++n)
            if (degree[n] != degree[n - 4]) { std::puts("FAIL Deja vu does not loop the phrase"); ok = false; break; }
    }
    if (ok) std::puts("PASS");
    return ok ? 0 : 1;
}
