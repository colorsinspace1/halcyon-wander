// Runs Wander's screen on the host for tools/screens.py: reads steps
// "seconds held sound rate chance scale dejavu length spread echo page" and prints frames as JSON.
#include "../display.cpp"
#include <cstdio>
int main() {
    hwander::DisplayState state = {};
    hwander::display_reset(state);
    hwander::Frame frame = {};
    double seconds, c[8]; int held, page; opxy::u32 serial = 0, offs = 0;
    std::printf("[");
    bool first = true, was = false;
    while (std::scanf("%lf %d %lf %lf %lf %lf %lf %lf %lf %lf %d", &seconds, &held, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &page) == 11) {
        const int frames = static_cast<int>(seconds * 60 + 0.5);
        for (int k = 0; k < (frames > 0 ? frames : 1); ++k) {
            opxy::draw::Input in = {};
            in.elapsed = 17; in.track = 1;
            if (held && !was) ++serial;
            if (!held && was) ++offs;
            was = held;
            in.note_serial = serial; in.held_notes = held ? 1 : 0; in.note_off_serial = offs;
            for (int i = 0; i < 8; ++i) in.slots[i] = static_cast<opxy::i32>(c[i] * 2147483647.0);
            for (int i = 0; i < 4; ++i) { in.shown[i] = static_cast<opxy::u8>(page ? i + 4 : i); in.parameters[i] = in.slots[in.shown[i]]; }
            hwander::display(state, in, frame);
        }
        if (!opxy::draw::valid(frame)) { std::fprintf(stderr, "invalid frame\n"); return 1; }
        std::printf("%s{\"nodes\":[", first ? "" : ","); first = false;
        for (unsigned i = 0; i < hwander::Frame::node_count; ++i) { const auto& n = frame.nodes[i];
            std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint)); }
        std::printf("],\"texts\":[");
        for (unsigned i = 0; i < hwander::Frame::text_count; ++i) { const auto& t = frame.texts[i];
            std::printf("%s[%d,%d,%d,\"%s\"]", i ? "," : "", t.x, t.y, t.scale, t.chars); }
        std::printf("]}");
    }
    std::printf("]\n");
}
