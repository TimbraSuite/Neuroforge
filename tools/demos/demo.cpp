// nf_demo - renders the end result of the three MANUAL.md tutorials as short bassline demos.
//   nf_demo <outdir>            writes tutorial-1.wav, tutorial-2.wav, tutorial-3.wav (stereo float32, 48 kHz)
//   nf_demo <outdir> 2          only tutorial 2
// Patches are built from parameter settings that mirror the tutorial steps (engine only, no UI).
#include "engine/Archetypes.h"
#include "engine/Capture.h"
#include "engine/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace nf;
using namespace nf::pid;

namespace {

constexpr double kBpm = 174.0;
constexpr int kSr = 48000;

struct Note { double s16, len16; int note; int vel; };
using Banks = std::array<std::shared_ptr<const WavetableBank>, kNumCaptures>;

// ---- patch helpers -------------------------------------------------------------------------
void setP(ParamState& s, const char* id, float real)
{
    int p = findParam(id);
    if (p < 0) { std::fprintf(stderr, "unknown param %s\n", id); std::exit(2); }
    s.setReal(p, real);
}

// Choosing an effect type in the UI loads that type's default values; overrides are real units.
void fx(ParamState& s, int slotIdx, FxType t, float p1 = NAN, float p2 = NAN, float p3 = NAN, float p4 = NAN,
        float mix = 1.f, float motionAmt = 1.f)
{
    s.setReal(slot(slotIdx, sType), (float) (int) t);
    const auto& info = getFxTypeInfo(t);
    const float v[4] = { p1, p2, p3, p4 };
    for (int k = 0; k < 4; ++k) {
        const auto& pi = info.params[(size_t) k];
        if (!pi.name) continue;
        s[slot(slotIdx, sP1 + k)] = fxRealToNorm(pi, std::isnan(v[k]) ? pi.def : v[k]);
    }
    s.setReal(slot(slotIdx, sMix), mix);
    s.setReal(slot(slotIdx, sMotion), motionAmt);
}

void route(ParamState& s, int row, ModSrc src, const char* dstParam, float amount)
{
    const int dstP = findParam(dstParam);
    const auto& dsts = modDestinations();
    int di = -1;
    for (int c = 1; c < (int) dsts.size(); ++c) if (dsts[(size_t) c] == dstP) di = c;
    if (di < 0) { std::fprintf(stderr, "param %s is not a mod destination\n", dstParam); std::exit(2); }
    s.setReal(mx(row, mxSrc), (float) (int) src);
    s.setReal(mx(row, mxDst), (float) di);
    s.setReal(mx(row, mxAmt), amount);
}

float divIndex(const char* name)
{
    const auto& n = rateDivNames();
    for (int i = 0; i < (int) n.size(); ++i) if (n[(size_t) i] == name) return (float) i;
    std::fprintf(stderr, "bad division %s\n", name); std::exit(2);
}

// ---- tutorial 1 : self-moving FM growl -----------------------------------------------------
ParamState tutorial1()
{
    ParamState s;                               // 1. INIT
    setP(s, "sub_oct", 0);                      //    sub Octave 0
    setP(s, "oa_table", 1);                     // 2. Basic Shapes, Position 0% (sine)
    setP(s, "oa_pos", 0);
    setP(s, "op_on", 1);                        // 3. FM OP: on, ratio 1, fine +5 ct, feedback 20 %
    setP(s, "op_coarse", 2);                    //    (choice index 2 = ratio "1")
    setP(s, "op_fine", 5);
    setP(s, "op_fb", 0.20f);
    setP(s, "oa_fm", 0.25f);                    //    OSC A FM Op 25 %
    setP(s, "env3_a", 40); setP(s, "env3_d", 130); setP(s, "env3_s", 0); setP(s, "env3_loop", 1); // 4.
    route(s, 0, ModSrc::Env3, "oa_fm", 0.70f);  // 5. ENV 3 -> FM Op, ring ~70 %
    fx(s, 0, FxType::Fold, 10.f, 0.40f, NAN, NAN, 1.f, 0.f);      // 6. Pass 1: Fold (motion 0 %) ...
    fx(s, 1, FxType::Filter, 4000.f, 0.35f);                      //    ... then Filter 4 kHz / 35 %
    setP(s, "mo1_mode", 0); setP(s, "mo1_sync", 1); setP(s, "mo1_div", divIndex("1/8"));
    setP(s, "mo1_shape", (float) (int) LfoShape::SawDown); setP(s, "mo1_depth", 0.50f);
    fx(s, 4, FxType::Comb, 200.f, 0.60f, NAN, 1.f, 0.35f);        // 7. Pass 2: Comb (key track, mix 35 %)
    fx(s, 5, FxType::Drive, 8.f, 0.80f);                          //    then Drive 8 dB / shape 80 %
    fx(s, 8, FxType::OTT, 0.50f);                                 // 8. Pass 3: OTT depth 50 %
    fx(s, 9, FxType::EQ, NAN, NAN, 350.f, -3.f);                  //    EQ mid 350 Hz / -3 dB
    setP(s, "sub_bus_level", -6);               // 9. Sub Level -6 dB
    return s;
}

// ---- tutorial 2 : talking bass with a vowel sequence -----------------------------------------
void vowelSteps(ParamState& s, int motionIdx)
{   // 4. bottom = A, 25 % = E, middle = I, 75 % = O, top = U : A O I E A U I O
    const float v[8] = { 0.f, 0.75f, 0.5f, 0.25f, 0.f, 1.f, 0.5f, 0.75f };
    for (int i = 0; i < 8; ++i) s[pid::motion(motionIdx, mStep0 + i)] = v[i];
}

ParamState tutorial2()
{
    ParamState s;                               // 1. Init, OSC A Saw>Square, unison 1 (Init defaults)
    setP(s, "oa_table", 0); setP(s, "oa_unison", 1);
    fx(s, 0, FxType::Drive, 12.f, NAN, NAN, NAN, 1.f, 0.f);       // 2. Pass 1 slot 1: Drive 12 dB, motion 0 %
    fx(s, 1, FxType::Formant, 2.f, 0.f, 0.65f, 0.f, 1.f, 1.f);    //    slot 2: Formant vowel I (middle), Q 65 %, mix 100 %, motion 100 %
    setP(s, "mo1_mode", 1); setP(s, "mo1_sync", 1); setP(s, "mo1_div", divIndex("1 bar")); // 3.
    setP(s, "mo1_depth", 1.0f); setP(s, "mo1_steps", 8); setP(s, "mo1_glide", 0.40f);
    vowelSteps(s, 0);
    return s;
}

// ---- tutorial 3 : two generations of resampling --------------------------------------------
void capture(const ParamState& st, Banks& banks, int slotIdx, const char* name)
{
    CaptureSettings cs; cs.note = 29 /*F1*/; cs.beats = 8; cs.maxFrames = 256; cs.bpm = kBpm;
    banks[(size_t) slotIdx] = renderCapture(st, banks, cs, name);
}

ParamState tutorial3(Banks& banks)
{
    ParamState s;
    for (auto& fp : factoryPresets()) if (fp.name == "Wormhole Reese") s = fp.state();     // 1. load preset
    // 2. NEXT GEN / RESAMPLE: capture F1, 8 beats, 256 frames -> Capture 1, load on OSC A, empty Forge
    capture(s, banks, 0, "Capture 1");
    s = nextGenerationState(s, 0);
    // 3. LFO 1: Sync, 1/4, Triangle -> OSC A Position
    setP(s, "lfo1_sync", 1); setP(s, "lfo1_div", divIndex("1/4")); setP(s, "lfo1_shape", (float) (int) LfoShape::Triangle);
    route(s, 0, ModSrc::Lfo1, "oa_pos", 0.5f);
    // 4. Pass 1: Comb (KeyTrack, 300 Hz, fb 75 %) then Drive 10 dB; Motion 1/8T depth 30 %
    fx(s, 0, FxType::Comb, 300.f, 0.75f, NAN, 1.f);
    fx(s, 1, FxType::Drive, 10.f);
    setP(s, "mo1_mode", 0); setP(s, "mo1_sync", 1); setP(s, "mo1_div", divIndex("1/8T"));
    setP(s, "mo1_shape", 0); setP(s, "mo1_depth", 0.30f);
    // 5. Pass 3: OTT depth 45 % then EQ
    fx(s, 8, FxType::OTT, 0.45f);
    fx(s, 9, FxType::EQ);
    // 6. NEXT GEN again: captures the patch above into Capture 2, loads it on OSC A with an empty Forge
    capture(s, banks, 1, "Capture 2");
    s = nextGenerationState(s, 1);
    //    new movement: Formant step sequence in Pass 2 (slot 5), plus the Position LFO again
    //    (NEXT GEN drops that route because Position was reset)
    fx(s, 4, FxType::Formant, 2.f, 0.f, 0.60f, 0.f, 1.f, 1.f);
    setP(s, "mo2_mode", 1); setP(s, "mo2_sync", 1); setP(s, "mo2_div", divIndex("1 bar"));
    setP(s, "mo2_depth", 0.8f); setP(s, "mo2_steps", 8); setP(s, "mo2_glide", 0.4f);
    vowelSteps(s, 1);
    route(s, 0, ModSrc::Lfo1, "oa_pos", 0.4f);
    return s;
}

// ---- note sequences (16th-note grid; F1 = 29, E1 = 28, G1 = 31, C2 = 36) ----------------------
constexpr int F1 = 29, E1 = 28, G1 = 31, C2 = 36, Bb1 = 34, Ab1 = 32;

struct Seq {
    std::vector<Note> n;
    void bar(int b, std::initializer_list<Note> l) { for (auto x : l) { x.s16 += b * 16; n.push_back(x); } }
};

std::vector<Note> line1()
{   // FM growl: syncopated 1/8 and 1/16 figures with held notes, F / E / G
    Seq q;
    for (int rep = 0; rep < 2; ++rep) {
        int o = rep * 4;
        q.bar(o + 0, { {0,6,F1,110}, {6,2,F1,100}, {8,4,F1,110}, {12,2,G1,105}, {14,2,F1,100} });
        q.bar(o + 1, { {0,3,F1,110}, {3,1,F1,95}, {4,2,F1,105}, {6,2,E1,105}, {8,8,E1,110} });
        q.bar(o + 2, { {0,6,F1,110}, {6,2,F1,100}, {8,2,F1,110}, {10,1,F1,95}, {11,1,F1,95}, {12,4,G1,110} });
        if (rep == 0) q.bar(o + 3, { {0,2,F1,110}, {2,2,G1,105}, {4,4,F1,110}, {8,2,C2,105}, {10,2,G1,105}, {12,4,F1,110} });
        else          q.bar(o + 3, { {0,2,F1,110}, {2,1,F1,95}, {3,1,F1,95}, {4,4,E1,110}, {8,2,G1,105}, {10,2,F1,105}, {12,4,F1,110} });
    }
    return q.n;
}

std::vector<Note> line2()
{   // talker: longer notes so the 8-step vowel loop (1 bar) is clearly audible
    Seq q;
    for (int rep = 0; rep < 2; ++rep) {
        int o = rep * 4;
        q.bar(o + 0, { {0,16,F1,110} });
        q.bar(o + 1, { {0,6,F1,110}, {6,2,F1,100}, {8,6,G1,110}, {14,2,F1,100} });
        q.bar(o + 2, { {0,8,F1,110}, {8,4,C2,105}, {12,4,Bb1,105} });
        q.bar(o + 3, { {0,4,F1,110}, {4,2,E1,105}, {6,2,E1,100}, {8,6,F1,110}, {14,2,G1,105} });
    }
    return q.n;
}

std::vector<Note> line3()
{   // resampled reese: mostly F1 (the capture pitch), slow movement, octave and fifth accents
    Seq q;
    for (int rep = 0; rep < 2; ++rep) {
        int o = rep * 4;
        q.bar(o + 0, { {0,10,F1,110}, {10,2,F1,100}, {12,4,G1,110} });
        q.bar(o + 1, { {0,8,F1,110}, {8,4,Ab1,105}, {12,4,F1,110} });
        q.bar(o + 2, { {0,6,F1,110}, {6,2,C2,105}, {8,6,F1,110}, {14,2,E1,100} });
        if (rep == 0) q.bar(o + 3, { {0,16,E1,110} });
        else          q.bar(o + 3, { {0,8,E1,110}, {8,8,F1,110} });
    }
    return q.n;
}

// ---- rendering -------------------------------------------------------------------------------
void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R)
{
    std::ofstream f(path, std::ios::binary);
    const uint32_t n = (uint32_t) L.size(), db = n * 8;
    auto u32 = [&](uint32_t v) { f.write((const char*) &v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*) &v, 2); };
    f.write("RIFF", 4); u32(36 + db); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(2); u32(kSr); u32(kSr * 8); u16(8); u16(32);
    f.write("data", 4); u32(db);
    for (uint32_t i = 0; i < n; ++i) { f.write((const char*) &L[i], 4); f.write((const char*) &R[i], 4); }
}

void render(const ParamState& st, const Banks& banks, const std::vector<Note>& notes, const std::string& path)
{
    Engine eng;
    constexpr int block = 256;
    eng.prepare(kSr, block);
    for (int i = 0; i < kNumCaptures; ++i) eng.setCapture(i, banks[(size_t) i]);
    const double spb = 60.0 / kBpm;
    const int lat = eng.getLatencySamples();
    const double bars = 8, tail = 1.2;
    const int total = (int) ((bars * 4 * spb + tail) * kSr) + lat;

    struct Ev { int sample; MidiEvent e; };
    std::vector<Ev> evs;
    for (auto& n : notes) {
        const double b0 = n.s16 * 0.25, len = std::max(0.2, n.len16 - 0.25) * 0.25; // small gap so notes retrigger cleanly
        evs.push_back({ (int) std::llround((b0 + len) * spb * kSr), { 0, 0x80, (uint8_t) n.note, 0 } });
        evs.push_back({ (int) std::llround(b0 * spb * kSr), { 0, 0x90, (uint8_t) n.note, (uint8_t) n.vel } });
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.sample < b.sample; });

    std::vector<float> L((size_t) total), R((size_t) total);
    Transport t; t.bpm = kBpm; t.playing = true;
    size_t ei = 0;
    std::vector<MidiEvent> blk;
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        blk.clear();
        while (ei < evs.size() && evs[ei].sample < pos + n) {
            auto e = evs[ei].e; e.offset = std::max(0, evs[ei].sample - pos); blk.push_back(e); ++ei;
        }
        t.ppq = pos / (double) kSr / spb;
        eng.render(L.data() + pos, R.data() + pos, n, st.v.data(), blk.data(), (int) blk.size(), t);
    }
    L.erase(L.begin(), L.begin() + lat); R.erase(R.begin(), R.begin() + lat);

    // short fade-in (click guard), 0.5 s fade-out, then normalise to -1 dBFS peak
    const int fade = (int) (0.5 * kSr), N = (int) L.size();
    for (int i = 0; i < fade; ++i) {
        float g = 0.5f + 0.5f * std::cos(3.14159265f * i / fade);
        L[(size_t) (N - fade + i)] *= g; R[(size_t) (N - fade + i)] *= g;
    }
    for (int i = 0; i < 64; ++i) { float g = i / 64.f; L[(size_t) i] *= g; R[(size_t) i] *= g; }
    float pk = 1e-9f;
    for (int i = 0; i < N; ++i) pk = std::max({ pk, std::abs(L[(size_t) i]), std::abs(R[(size_t) i]) });
    const float g = std::pow(10.f, -1.f / 20.f) / pk;
    for (int i = 0; i < N; ++i) { L[(size_t) i] *= g; R[(size_t) i] *= g; }
    writeWav(path, L, R);
    std::printf("%s  %.2fs  raw peak %.2f dBFS (normalised to -1 dBFS, gain %+.1f dB)\n", path.c_str(), N / (double) kSr,
                20 * std::log10(pk), 20 * std::log10(g));
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "demos";
    const int only = argc > 2 ? std::atoi(argv[2]) : 0;
    std::filesystem::create_directories(dir);
    Banks none {};
    if (!only || only == 1) render(tutorial1(), none, line1(), dir + "/tutorial-1.wav");
    if (!only || only == 2) render(tutorial2(), none, line2(), dir + "/tutorial-2.wav");
    if (!only || only == 3) {
        Banks banks {};
        auto st = tutorial3(banks);
        render(st, banks, line3(), dir + "/tutorial-3.wav");
    }
    return 0;
}
