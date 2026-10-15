#pragma once
// Central parameter registry. Every parameter is stored normalized 0..1 (that is what hosts
// automate, what presets store and what the mod matrix offsets). Engine code maps to real units.
#include <string>
#include <vector>

namespace nf {

enum class Curve : int { Lin, Log, Disc, Toggle };

struct ParamDef {
    std::string id;     // stable id used in state/presets, e.g. "oa_pos"
    std::string name;   // display name, e.g. "Osc A Position"
    float min = 0.f, max = 1.f, def = 0.f; // real units; def in real units
    Curve curve = Curve::Lin;
    std::vector<std::string> choices;      // for Disc params with named steps (min=0,max=n-1)
    const char* unit = "";
    bool voice = false;       // modulated per voice
    bool modulatable = true;  // can be a mod-matrix destination
};

namespace pid {
// ---- block layouts (offsets inside a block) ----
enum Global { masterGain, masterClip, quality, voiceMode, glide, bendRange, guardOn, guardFreq,
              subDrive, subLevel, midLevel, midWidth, ampVel, pitchEnv, pitchEnvSrc, numGlobal };
enum Osc { oOn, oTable, oOct, oSemi, oFine, oLevel, oPan, oPos, oUni, oDetune, oStereo, oBlend,
           oPhase, oPhaseRand, oWarp, oWarpAmt, oFm, oRetrig, numOsc };
enum Op { opOn, opCoarse, opFine, opFb, opShape, numOp };
enum Sub { subOn, subOct, subShape, subLevelP, subDirect, numSub };
enum Noise { nLevel, nColor, numNoise };
enum VFilter { vfOn, vfMode, vfCutoff, vfReso, vfDrive, vfKey, vfEnv, numVf };
enum Env { eA, eD, eS, eR, eLoop, numEnv };
enum Lfo { lSync, lRate, lDiv, lShape, lRetrig, lPhase, numLfo };
enum Motion { mMode, mSync, mRate, mDiv, mShape, mDepth, mSteps, mGlide, mStep0, numMotion = mStep0 + 16 };
enum Pass { pOn, pMix, numPass };
enum Slot { sType, sP1, sP2, sP3, sP4, sMix, sMotion, numSlot };
enum MatrixSlot { mxSrc, mxDst, mxAmt, numMx };
// Curve stepper (Massive "Performer"-style): up to 16 steps, each a curve shape with its own intensity.
enum Stepper { stSync, stRate, stDiv, stSteps, stSmooth, stRetrig, stShape0, stAmt0 = stShape0 + 16,
               numStepper = stAmt0 + 16 };
constexpr int kStepperSteps = 16;

constexpr int kNumEnvs = 3, kNumLfos = 4, kNumPasses = 3, kSlotsPerPass = 4,
              kNumSlots = kNumPasses * kSlotsPerPass, kNumMacros = 8, kNumMatrix = 24, kNumSteppers = 2;

// ---- block bases ----
constexpr int oscA   = numGlobal;
constexpr int oscB   = oscA + numOsc;
constexpr int op     = oscB + numOsc;
constexpr int sub    = op + numOp;
constexpr int noise  = sub + numSub;
constexpr int vf     = noise + numNoise;
constexpr int env0   = vf + numVf;                       // env i: env0 + i*numEnv
constexpr int lfo0   = env0 + kNumEnvs * numEnv;         // lfo i
constexpr int motion0= lfo0 + kNumLfos * numLfo;         // motion i (one per pass)
constexpr int pass0  = motion0 + kNumPasses * numMotion; // pass i
constexpr int slot0  = pass0 + kNumPasses * numPass;     // slot i (pass = i / 4)
constexpr int macro0 = slot0 + kNumSlots * numSlot;      // macro i
constexpr int mx0    = macro0 + kNumMacros;              // matrix slot i
constexpr int step0  = mx0 + kNumMatrix * numMx;         // stepper i (appended last: keeps older ids stable)
constexpr int count  = step0 + kNumSteppers * numStepper;

constexpr int osc(int which, int off)  { return (which == 0 ? oscA : oscB) + off; }
constexpr int env(int i, int off)      { return env0 + i * numEnv + off; }
constexpr int lfo(int i, int off)      { return lfo0 + i * numLfo + off; }
constexpr int motion(int i, int off)   { return motion0 + i * numMotion + off; }
constexpr int pass(int i, int off)     { return pass0 + i * numPass + off; }
constexpr int slot(int i, int off)     { return slot0 + i * numSlot + off; }
constexpr int macro(int i)             { return macro0 + i; }
constexpr int mx(int i, int off)       { return mx0 + i * numMx + off; }
constexpr int stepper(int i, int off)  { return step0 + i * numStepper + off; }
} // namespace pid

// Modulation sources (index stored in matrix "src" param)
enum class ModSrc : int {
    None = 0, Env1, Env2, Env3, Lfo1, Lfo2, Lfo3, Lfo4, Motion1, Motion2, Motion3,
    Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
    Velocity, Note, ModWheel, PitchBend, Aftertouch, Random,
    Step1, Step2, // added in state version 2 (appended so older indices keep their meaning)
    NumSources
};
bool modSrcIsVoice(ModSrc s);
const std::vector<std::string>& modSrcNames();

// LFO / motion rate divisions (in beats; 1 beat = quarter note)
const std::vector<std::string>& rateDivNames();
double rateDivBeats(int index);

enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, NumShapes };
const std::vector<std::string>& lfoShapeNames();

// Curve-stepper shape library. stepCurve(shape, t): t in [0,1) across one step -> value 0..1.
const std::vector<std::string>& stepCurveNames();
float stepCurve(int shape, float t);

// Parameter-state versions. v1 had 25 modulation sources (None..Random); v2 added Step 1/2.
constexpr int kStateVersion = 2;
constexpr int kModSrcCountV1 = 25;
bool isMatrixSourceParam(int id);
float remapModSrcFromV1(float norm); // normalized src value saved by v1 -> current normalized value

const std::vector<ParamDef>& paramDefs();              // size == pid::count
const std::vector<int>& modDestinations();             // param indices usable as destinations (matrix dst choice i -> param)
int findParam(const std::string& id);                  // -1 if missing

float normToReal(const ParamDef& d, float norm);
float realToNorm(const ParamDef& d, float real);
inline float normToReal(int id, float norm) { return normToReal(paramDefs()[id], norm); }
inline float realToNorm(int id, float real) { return realToNorm(paramDefs()[id], real); }

// Plain container for a full parameter state (normalized).
struct ParamState {
    std::vector<float> v;
    ParamState();
    float& operator[](int i) { return v[(size_t) i]; }
    float operator[](int i) const { return v[(size_t) i]; }
    void setReal(int id, float real) { v[(size_t) id] = realToNorm(id, real); }
    float real(int id) const { return normToReal(id, v[(size_t) id]); }
    std::string toText() const;                  // "id value\n" lines (normalized)
    bool fromText(const std::string& text);      // missing ids keep defaults
};

} // namespace nf
