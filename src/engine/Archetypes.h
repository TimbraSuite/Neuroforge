#pragma once
// Patch generation: archetype generator, Mutate, and factory presets.
#include "Params.h"
#include <cstdint>
#include <string>
#include <vector>

namespace nf {

enum class Archetype : int { Reese, FmGrowl, Talker, Metallic, Tearer, Hoover, Stab, Liquid, Donk, NumArchetypes };
const std::vector<std::string>& archetypeNames();

// A complete, mix-ready patch with macros wired (GROWL TALK GRIT METAL MOTION WIDTH SUB TONE).
ParamState generatePatch(Archetype a, uint32_t seed);

// Nudge a patch. amount 0..1 (0.15 subtle, 0.4 medium, 0.8 wild). Protects levels, sub and Low Guard.
void mutatePatch(ParamState& s, float amount, uint32_t seed);

struct FactoryPreset {
    std::string name, category; // category = archetype name
    Archetype archetype;
    uint32_t seed;
    ParamState state() const { return generatePatch(archetype, seed); } // built on demand (~50 ms)
};
const std::vector<FactoryPreset>& factoryPresets();

} // namespace nf
