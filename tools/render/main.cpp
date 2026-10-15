// nf_render — deterministic offline renders of Neuroforge patches.
//   nf_render --arch "FM Growl" --seed 3 --out renders/growl.wav
//   nf_render --preset "Wormhole Reese" --out x.wav        (factory preset by name)
//   nf_render --file my.nfpreset --out x.wav
//   nf_render --all renders/all                             (every factory preset + archetypes x 3 seeds)
// Options: --riff neuro|hold|stabs  --bpm 174  --sr 48000  --quality 0|1|2  --set id=real ...
//          --dump file.nfpreset (write the patch text)
#include "engine/Archetypes.h"
#include "engine/Engine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace nf;

namespace {

struct Note { double beat, len; int note; int vel; };

std::vector<Note> makeRiff(const std::string& kind)
{
    // F1 = 29 (43.65 Hz). 1 beat = quarter note; 16th = 0.25.
    std::vector<Note> r;
    auto add = [&](double sixteenth, double lenSixteenths, int note, int vel = 110) {
        r.push_back({ sixteenth * 0.25, lenSixteenths * 0.25, note, vel });
    };
    if (kind == "hold") {
        add(0, 32, 29); // 2 bars held F1 — shows pure movement
    } else if (kind == "stabs") {
        for (int i = 0; i < 8; ++i) add(i * 4, 1.5, i % 3 == 2 ? 36 : 29);
    } else { // "neuro": 2 bars of riff + 2 bars held note
        add(0, 3, 29);  add(3, 1, 29);  add(6, 2, 41); add(8, 3, 29);
        add(11, 1, 32); add(12, 2, 29); add(14, 2, 34);
        add(16, 2, 29); add(18, 1, 29); add(19, 3, 36); add(22, 2, 29);
        add(24, 1, 39); add(25, 1, 37); add(26, 2, 36); add(28, 4, 29);
        add(32, 30, 29);
    }
    return r;
}

bool writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t n = (uint32_t) L.size(), dataBytes = n * 2 * 4;
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(2); u32((uint32_t) sr); u32((uint32_t) sr * 8); u16(8); u16(32);
    f.write("data", 4); u32(dataBytes);
    for (uint32_t i = 0; i < n; ++i) { f.write((const char*) &L[i], 4); f.write((const char*) &R[i], 4); }
    return true;
}

struct RenderResult { double seconds = 0, cpuMs = 0; float peak = 0; };

RenderResult renderPatch(const ParamState& st, const std::string& riff, double bpm, int sr, const std::string& out)
{
    Engine eng;
    constexpr int block = 256;
    eng.prepare(sr, block);
    auto notes = makeRiff(riff);
    double endBeat = 0;
    for (auto& n : notes) endBeat = std::max(endBeat, n.beat + n.len);
    const double secPerBeat = 60.0 / bpm;
    const int lat = eng.getLatencySamples();
    const int total = (int) ((endBeat * secPerBeat + 0.6) * sr) + lat;

    struct Ev { int sample; MidiEvent e; };
    std::vector<Ev> evs;
    for (auto& n : notes) {
        int on = (int) std::llround(n.beat * secPerBeat * sr), off = (int) std::llround((n.beat + n.len) * secPerBeat * sr);
        evs.push_back({ off, { 0, 0x80, (uint8_t) n.note, 0 } }); // offs first at equal time (legato-safe ordering below)
        evs.push_back({ on, { 0, 0x90, (uint8_t) n.note, (uint8_t) n.vel } });
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.sample < b.sample; });

    std::vector<float> L((size_t) total), R((size_t) total);
    Transport t; t.bpm = bpm; t.playing = true;
    size_t ei = 0;
    std::vector<MidiEvent> blockEv;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        blockEv.clear();
        while (ei < evs.size() && evs[ei].sample < pos + n) {
            auto e = evs[ei++].e; e.offset = std::max(0, (int) evs[ei - 1].sample - pos); blockEv.push_back(e);
        }
        t.ppq = pos / (double) sr / secPerBeat;
        eng.render(L.data() + pos, R.data() + pos, n, st.v.data(), blockEv.data(), (int) blockEv.size(), t);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    // remove decimator latency so renders line up with the grid
    L.erase(L.begin(), L.begin() + lat); R.erase(R.begin(), R.begin() + lat);

    RenderResult rr;
    rr.seconds = (double) L.size() / sr;
    rr.cpuMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    for (size_t i = 0; i < L.size(); ++i) rr.peak = std::max({ rr.peak, std::abs(L[i]), std::abs(R[i]) });
    writeWav(out, L, R, sr);
    return rr;
}

std::string slug(std::string s)
{
    for (auto& c : s) if (!std::isalnum((unsigned char) c)) c = '_';
    return s;
}

int archIndex(const std::string& name)
{
    const auto& n = archetypeNames();
    for (int i = 0; i < (int) n.size(); ++i) if (n[(size_t) i] == name) return i;
    return -1;
}

void report(const std::string& name, const RenderResult& r)
{
    std::printf("%-40s %5.2fs  peak %6.2f dBFS  cpu %6.1f ms  (%.1f%% of realtime)\n", name.c_str(), r.seconds,
                20 * std::log10(std::max(1e-9f, r.peak)), r.cpuMs, 100.0 * r.cpuMs / 1000.0 / r.seconds);
}

} // namespace

int main(int argc, char** argv)
{
    std::string arch, preset, file, out = "renders/out.wav", riff = "neuro", all, dump;
    uint32_t seed = 1;
    double bpm = 174;
    int sr = 48000, quality = -1;
    std::vector<std::pair<std::string, float>> sets;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--arch") arch = next();
        else if (a == "--seed") seed = (uint32_t) std::stoul(next());
        else if (a == "--preset") preset = next();
        else if (a == "--file") file = next();
        else if (a == "--out") out = next();
        else if (a == "--riff") riff = next();
        else if (a == "--bpm") bpm = std::stod(next());
        else if (a == "--sr") sr = std::stoi(next());
        else if (a == "--quality") quality = std::stoi(next());
        else if (a == "--all") all = next();
        else if (a == "--dump") dump = next();
        else if (a == "--set") {
            std::string kv = next(); auto eq = kv.find('=');
            if (eq != std::string::npos) sets.push_back({ kv.substr(0, eq), std::stof(kv.substr(eq + 1)) });
        } else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }

    auto finish = [&](ParamState st) {
        for (auto& [id, v] : sets) {
            int p = findParam(id);
            if (p < 0) std::fprintf(stderr, "unknown param %s\n", id.c_str()); else st.setReal(p, v);
        }
        if (quality >= 0) st.setReal(pid::quality, (float) quality);
        return st;
    };

    if (!all.empty()) {
        for (auto& fp : factoryPresets()) {
            auto r = renderPatch(finish(fp.state()), riff, bpm, sr, all + "/" + slug(fp.category + "__" + fp.name) + ".wav");
            report(fp.category + " / " + fp.name, r);
        }
        for (int a = 0; a < (int) Archetype::NumArchetypes; ++a)
            for (uint32_t s = 1; s <= 3; ++s) {
                auto name = archetypeNames()[(size_t) a];
                auto r = renderPatch(finish(generatePatch((Archetype) a, s)), riff, bpm, sr,
                                     all + "/gen__" + slug(name) + "_" + std::to_string(s) + ".wav");
                report("gen " + name + " #" + std::to_string(s), r);
            }
        return 0;
    }

    ParamState st;
    std::string label = "init";
    if (!arch.empty()) {
        int a = archIndex(arch);
        if (a < 0) { std::fprintf(stderr, "unknown archetype %s\n", arch.c_str()); return 2; }
        st = generatePatch((Archetype) a, seed); label = arch + " #" + std::to_string(seed);
    } else if (!preset.empty()) {
        bool found = false;
        for (auto& fp : factoryPresets()) if (fp.name == preset) { st = fp.state(); found = true; }
        if (!found) { std::fprintf(stderr, "unknown preset %s\n", preset.c_str()); return 2; }
        label = preset;
    } else if (!file.empty()) {
        std::ifstream f(file); std::stringstream ss; ss << f.rdbuf();
        if (!st.fromText(ss.str())) { std::fprintf(stderr, "could not read %s\n", file.c_str()); return 2; }
        label = file;
    }
    st = finish(st);
    if (!dump.empty()) { std::ofstream(dump) << st.toText(); }
    report(label, renderPatch(st, riff, bpm, sr, out));
    return 0;
}
