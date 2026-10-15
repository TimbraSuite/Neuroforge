#include "Panels.h"
#include <cmath>

using namespace nf;

namespace nfui {

//==============================================================================================
// TabHost
//==============================================================================================
Page* TabHost::addTab(const juce::String& name)
{
    const int i = buttons.size();
    auto* b = buttons.add(new juce::TextButton(name));
    addAndMakeVisible(b);
    b->onClick = [this, i] { select(i); };
    auto* pg = pages.add(new Page());
    addChildComponent(pg);
    if (i == 0) select(0);
    return pg;
}

void TabHost::select(int i)
{
    current = i;
    for (int j = 0; j < buttons.size(); ++j) {
        buttons[j]->setToggleState(j == i, juce::dontSendNotification);
        pages[j]->setVisible(j == i);
    }
}

void TabHost::resized()
{
    for (int j = 0; j < buttons.size(); ++j) buttons[j]->setBounds(j * (tabW + 4), 0, tabW, barH);
    for (auto* p : pages) p->setBounds(0, barH + 4, getWidth(), getHeight() - barH - 4);
}

//==============================================================================================
// small helpers
//==============================================================================================
static void placeCells(const std::vector<juce::Component*>& comps, juce::Rectangle<int> area, int cols, int cellH,
                       int gap = 0)
{
    if (cols <= 0) return;
    const int cw = area.getWidth() / cols;
    for (size_t i = 0; i < comps.size(); ++i) {
        const int cx = (int) i % cols, cy = (int) i / cols;
        comps[i]->setBounds(area.getX() + cx * cw, area.getY() + cy * (cellH + gap), cw, cellH);
    }
}

static void heading(juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r)
{
    g.setColour(cDim);
    g.setFont(font(10.f, true));
    g.drawText(t, r, juce::Justification::centredLeft);
    g.setColour(cEdge);
    g.drawHorizontalLine(r.getCentreY(), (float) r.getX() + g.getCurrentFont().getStringWidthFloat(t) + 8.f, (float) r.getRight());
}

//==============================================================================================
// SOURCE panel
//==============================================================================================
static void buildOscPage(Page& pg, NeuroforgeProcessor& p, int o)
{
    using namespace pid;
    auto* on = pg.make<ParamToggle>(p, osc(o, oOn), "On");
    auto* table = pg.make<ParamCombo>(p, osc(o, oTable));
    auto* view = pg.make<WavetableView>(p, o);
    auto* warp = pg.make<ParamCombo>(p, osc(o, oWarp));
    auto* retrig = pg.make<ParamToggle>(p, osc(o, oRetrig), "Retrig");
    std::vector<juce::Component*> knobs;
    struct K { int off; const char* lbl; };
    for (K k : { K { oOct, "" }, K { oSemi, "" }, K { oFine, "" }, K { oLevel, "" }, K { oPan, "" },
                 K { oPos, "Position" }, K { oUni, "" }, K { oDetune, "" }, K { oStereo, "" }, K { oBlend, "" },
                 K { oPhase, "" }, K { oPhaseRand, "Rand" }, K { oWarpAmt, "Warp" }, K { oFm, "FM Op" } })
        knobs.push_back(pg.make<Knob>(p, osc(o, k.off), k.lbl));
    pg.layout = [=](juce::Rectangle<int> r) {
        auto top = r.removeFromTop(24);
        on->setBounds(top.removeFromLeft(56));
        table->setBounds(top.reduced(2, 0));
        r.removeFromTop(4);
        view->setBounds(r.removeFromTop(104));
        r.removeFromTop(4);
        auto row = r.removeFromTop(24);
        retrig->setBounds(row.removeFromRight(70));
        warp->setBounds(row.reduced(0, 0).withTrimmedRight(4));
        r.removeFromTop(6);
        placeCells(knobs, r, 5, 78);
    };
}

static void buildFmPage(Page& pg, NeuroforgeProcessor& p)
{
    using namespace pid;
    auto* on = pg.make<ParamToggle>(p, op + opOn, "FM Op On");
    auto* ratio = pg.make<ParamCombo>(p, op + opCoarse);
    std::vector<juce::Component*> knobs { pg.make<Knob>(p, op + opFine, "Fine"), pg.make<Knob>(p, op + opFb, "Feedback"),
                                          pg.make<Knob>(p, op + opShape, "Shape") };
    pg.painter = [](juce::Graphics& g) {
        g.setColour(cDim);
        g.setFont(font(11.f));
        g.drawFittedText("The FM operator is a sine-family modulator. Raise \"FM Op\" on Osc A / Osc B to route it into "
                         "the oscillators' phase. Move the index with a looping Env 2 / Env 3 for classic Reptilian growl.",
                         juce::Rectangle<int>(6, 190, 290, 90), juce::Justification::topLeft, 6);
    };
    pg.layout = [=](juce::Rectangle<int> r) {
        auto top = r.removeFromTop(24);
        on->setBounds(top.removeFromLeft(100));
        r.removeFromTop(6);
        auto row = r.removeFromTop(24);
        row.removeFromLeft(0);
        ratio->setBounds(row.removeFromLeft(140));
        r.removeFromTop(10);
        placeCells(knobs, r.removeFromTop(78), 4, 78);
    };
}

static void buildSubNoisePage(Page& pg, NeuroforgeProcessor& p)
{
    using namespace pid;
    auto* subOnT = pg.make<ParamToggle>(p, sub + subOn, "Sub On");
    auto* direct = pg.make<ParamToggle>(p, sub + subDirect, "Direct (bypass Forge)");
    std::vector<juce::Component*> subK { pg.make<Knob>(p, sub + subOct, "Octave"), pg.make<Knob>(p, sub + subShape, "Shape"),
                                         pg.make<Knob>(p, sub + subLevelP, "Level") };
    std::vector<juce::Component*> noiseK { pg.make<Knob>(p, noise + nLevel, "Level"), pg.make<Knob>(p, noise + nColor, "Color") };
    pg.painter = [](juce::Graphics& g) {
        heading(g, "SUB OSCILLATOR", { 0, 0, 300, 14 });
        heading(g, "NOISE", { 0, 150, 300, 14 });
    };
    pg.layout = [=](juce::Rectangle<int> r) {
        auto row = r.removeFromTop(14); juce::ignoreUnused(row);
        auto t = r.removeFromTop(24);
        subOnT->setBounds(t.removeFromLeft(80));
        direct->setBounds(t);
        placeCells(subK, r.removeFromTop(84).withTrimmedTop(4), 4, 78);
        r.removeFromTop(22);
        placeCells(noiseK, r.removeFromTop(84).withTrimmedTop(14), 4, 78);
    };
}

static juce::String filterModeText(float norm)
{
    static const char* n[] = { "LP", "BP", "HP", "Notch" };
    const float r = norm * 3.f;
    const int i = juce::jlimit(0, 2, (int) std::floor(r));
    const float f = r - (float) i;
    if (norm >= 0.999f) return n[3];
    if (f < 0.08f) return n[i];
    if (f > 0.92f) return n[i + 1];
    return juce::String(n[i]) + ">" + n[i + 1] + " " + juce::String((int) (f * 100.f)) + "%";
}

static void buildFilterPitchPage(Page& pg, NeuroforgeProcessor& p)
{
    using namespace pid;
    auto* vfOnT = pg.make<ParamToggle>(p, vf + vfOn, "Voice Filter");
    std::vector<juce::Component*> fk;
    auto* mode = pg.make<Knob>(p, vf + vfMode, "Mode");
    mode->setFormatter([](float n) { return filterModeText(n); });
    fk.push_back(mode);
    fk.push_back(pg.make<Knob>(p, vf + vfCutoff, "Cutoff"));
    fk.push_back(pg.make<Knob>(p, vf + vfReso, "Reso"));
    fk.push_back(pg.make<Knob>(p, vf + vfDrive, "Drive"));
    fk.push_back(pg.make<Knob>(p, vf + vfKey, "Keytrack"));
    fk.push_back(pg.make<Knob>(p, vf + vfEnv, "Env 2"));
    auto* penv = pg.make<Knob>(p, pitchEnv, "Pitch Env");
    auto* psrc = pg.make<ParamCombo>(p, pitchEnvSrc);
    auto* gl = pg.make<Knob>(p, glide, "Glide");
    auto* bend = pg.make<Knob>(p, bendRange, "Bend");
    auto* vel = pg.make<Knob>(p, ampVel, "Vel Sens");
    auto* vm = pg.make<ParamCombo>(p, voiceMode);
    pg.painter = [](juce::Graphics& g) {
        heading(g, "VOICE FILTER", { 0, 0, 300, 14 });
        heading(g, "PITCH / VOICE", { 0, 196, 300, 14 });
    };
    pg.layout = [=](juce::Rectangle<int> r) {
        r.removeFromTop(14);
        vfOnT->setBounds(r.removeFromTop(24).removeFromLeft(110));
        placeCells(fk, r.removeFromTop(160).withTrimmedTop(4), 4, 78, 2);
        r.removeFromTop(14);
        auto row = r.removeFromTop(78).withTrimmedTop(2);
        const int cw = row.getWidth() / 4;
        penv->setBounds(row.removeFromLeft(cw));
        gl->setBounds(row.removeFromLeft(cw));
        bend->setBounds(row.removeFromLeft(cw));
        vel->setBounds(row.removeFromLeft(cw));
        auto row2 = r.removeFromTop(26);
        psrc->setBounds(row2.removeFromLeft(100));
        row2.removeFromLeft(8);
        vm->setBounds(row2.removeFromLeft(100));
    };
}

std::unique_ptr<Page> makeSourcePanel(NeuroforgeProcessor& p)
{
    auto root = std::make_unique<Page>();
    auto* tabs = root->make<TabHost>(24, 58);
    buildOscPage(*tabs->addTab("OSC A"), p, 0);
    buildOscPage(*tabs->addTab("OSC B"), p, 1);
    buildFmPage(*tabs->addTab("FM OP"), p);
    buildSubNoisePage(*tabs->addTab("SUB+NZ"), p);
    buildFilterPitchPage(*tabs->addTab("FILTER"), p);
    Page* rp = root.get();
    root->painter = [rp](juce::Graphics& g) { paintPanel(g, rp->getLocalBounds()); };
    root->layout = [tabs](juce::Rectangle<int> r) { tabs->setBounds(r.reduced(8, 8)); };
    return root;
}

//==============================================================================================
// FORGE
//==============================================================================================
SlotStrip::SlotStrip(NeuroforgeProcessor& p, int s) : proc(p), slot(s)
{
    using namespace pid;
    type = make<ParamCombo>(p, pid::slot(s, sType));
    addBtn = make<juce::TextButton>("+ add FX");
    remBtn = make<juce::TextButton>("x");
    remBtn->setTooltip("Remove effect");
    for (int k = 0; k < 4; ++k) pk[k] = make<Knob>(p, pid::slot(s, sP1 + k));
    mix = make<Knob>(p, pid::slot(s, sMix), "Mix");
    mot = make<Knob>(p, pid::slot(s, sMotion), "Motion");
    mot->setTooltip("Motion amount (bipolar): how much the pass Motion modulates this slot's first parameter");

    addBtn->onClick = [this] {
        juce::PopupMenu m;
        for (int t = 1; t < (int) FxType::NumTypes; ++t) m.addItem(t, getFxTypeInfo((FxType) t).name);
        juce::Component::SafePointer<SlotStrip> sp(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addBtn), [sp](int r) {
            if (sp != nullptr && r > 0) sp->proc.setParamReal(pid::slot(sp->slot, pid::sType), (float) r);
        });
    };
    remBtn->onClick = [this] { proc.setParamReal(pid::slot(slot, pid::sType), 0.f); };
    type->onIndexChanged = [this](int i) { applyType(i); };

    painter = [this](juce::Graphics& g) {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        if (off) {
            g.setColour(cEdge.withAlpha(0.6f));
            g.drawRoundedRectangle(r, 4.f, 1.f);
        } else {
            g.setColour(cPanel);
            g.fillRoundedRectangle(r, 4.f);
            paintMetal(g, r, 4.f, getX() * 13 + getY() * 7);
            g.setColour(cEdge);
            g.drawRoundedRectangle(r, 4.f, 1.f);
        }
    };
    applyType(type->getIndex());
}

void SlotStrip::applyType(int t)
{
    off = (t == 0);
    const auto& info = getFxTypeInfo((FxType) juce::jlimit(0, (int) FxType::NumTypes - 1, t));
    for (int k = 0; k < 4; ++k) {
        const auto& pi = info.params[(size_t) k];
        const bool used = !off && pi.name != nullptr;
        pk[k]->setVisible(used);
        if (used) {
            pk[k]->setLabel(pi.name);
            pk[k]->setBipolar(pi.min < 0.f && pi.max > 0.f);
            pk[k]->setFormatter([pi](float n) { return nfFormatFx(pi, n); });
        }
    }
    type->setVisible(!off);
    remBtn->setVisible(!off);
    mix->setVisible(!off);
    mot->setVisible(!off);
    addBtn->setVisible(off);
    resized();
    repaint();
    if (onHeightChange) onHeightChange();
}

void SlotStrip::resized()
{
    auto r = getLocalBounds();
    if (off) { addBtn->setBounds(r.reduced(1)); return; }
    auto row = r.removeFromTop(24).reduced(2, 2);
    remBtn->setBounds(row.removeFromRight(20));
    row.removeFromRight(3);
    type->setBounds(row);
    auto kr = r.reduced(2, 0).withTrimmedBottom(2);
    const int cw = kr.getWidth() / 6;
    for (int k = 0; k < 4; ++k) pk[k]->setBounds(kr.getX() + k * cw, kr.getY(), cw, kr.getHeight());
    mix->setBounds(kr.getX() + 4 * cw, kr.getY(), cw, kr.getHeight());
    mot->setBounds(kr.getX() + 5 * cw, kr.getY(), cw, kr.getHeight());
}

//----------------------------------------------------------------------------------------------
MotionEditor::MotionEditor(NeuroforgeProcessor& p, int m)
{
    using namespace pid;
    mode = make<ParamCombo>(p, motion(m, mMode));
    sync = make<ParamToggle>(p, motion(m, mSync), "Sync");
    div = make<ParamCombo>(p, motion(m, mDiv));
    rate = make<ParamBar>(p, motion(m, mRate), "Rate");
    shape = make<ParamCombo>(p, motion(m, mShape));
    trace = make<TraceView>(p, [&p, m] { return p.getEngine().uiMotion[m].load(std::memory_order_relaxed); },
                            juce::Colour(0xffc58cff));
    seq = make<StepSeq>(p, m);
    depth = make<Knob>(p, motion(m, mDepth), "Depth");
    steps = make<Knob>(p, motion(m, mSteps), "Steps");
    glide = make<Knob>(p, motion(m, mGlide), "Glide");
    mode->onIndexChanged = [this](int i) { stepsMode = (i == 1); resized(); };
    sync->onToggled = [this](bool b) { synced = b; resized(); };
    stepsMode = mode->getIndex() == 1;
    synced = sync->getToggleState();
    painter = [](juce::Graphics& g) {
        auto r = juce::Rectangle<float>(0.f, 0.f, 1000.f, 1000.f);
        juce::ignoreUnused(g, r);
    };
}

void MotionEditor::resized()
{
    auto r = getLocalBounds();
    if (r.isEmpty()) return;
    auto row1 = r.removeFromTop(22);
    mode->setBounds(row1.removeFromLeft(86));
    row1.removeFromLeft(4);
    sync->setBounds(row1);
    r.removeFromTop(3);
    auto row2 = r.removeFromTop(22);
    div->setVisible(synced);
    rate->setVisible(!synced);
    shape->setVisible(!stepsMode);
    if (!stepsMode) shape->setBounds(row2.removeFromRight(86));
    row2.removeFromRight(3);
    div->setBounds(row2);
    rate->setBounds(row2);
    r.removeFromTop(3);
    auto viz = r.removeFromTop(36);
    trace->setVisible(!stepsMode);
    seq->setVisible(stepsMode);
    trace->setBounds(viz);
    seq->setBounds(viz);
    r.removeFromTop(2);
    steps->setVisible(stepsMode);
    glide->setVisible(stepsMode);
    const int cw = r.getWidth() / 3;
    depth->setBounds(r.getX(), r.getY(), cw, r.getHeight());
    steps->setBounds(r.getX() + cw, r.getY(), cw, r.getHeight());
    glide->setBounds(r.getX() + 2 * cw, r.getY(), cw, r.getHeight());
}

//----------------------------------------------------------------------------------------------
PassColumn::PassColumn(NeuroforgeProcessor& p, int i) : idx(i)
{
    using namespace pid;
    on = make<ParamToggle>(p, pass(i, pOn), "");
    chip = make<ModChip>(p, (int) ModSrc::Motion1 + i, "MOTION " + juce::String(i + 1));
    mixBar = make<ParamBar>(p, pass(i, pMix), "Mix");
    motion = make<MotionEditor>(p, i);
    for (int s = 0; s < 4; ++s) {
        slots[s] = make<SlotStrip>(p, i * 4 + s);
        slots[s]->onHeightChange = [this] { resized(); repaint(); };
    }
    painter = [this](juce::Graphics& g) {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(cPanel2);
        g.fillRoundedRectangle(r, 6.f);
        paintMetal(g, r, 6.f, getX() * 29 + 5);
        g.setColour(cEdge);
        g.drawRoundedRectangle(r, 6.f, 1.f);
        g.setColour(cText);
        g.setFont(font(13.f, true));
        g.drawText("PASS " + juce::String(idx + 1), 56, 6, 60, 22, juce::Justification::centredLeft);
    };
}

void PassColumn::resized()
{
    auto r = getLocalBounds().reduced(6, 6);
    auto head = r.removeFromTop(26);
    on->setBounds(head.removeFromLeft(44).reduced(0, 3));
    head.removeFromLeft(62);
    auto mixArea = head.removeFromRight(58);
    mixBar->setBounds(mixArea.reduced(0, 3));
    chip->setBounds(head.reduced(2, 3));
    r.removeFromTop(4);
    motion->setBounds(r.removeFromTop(146));
    r.removeFromTop(6);
    for (auto* s : slots) {
        const int h = s->getDesiredHeight();
        s->setBounds(r.removeFromTop(h));
        r.removeFromTop(4);
    }
}

std::unique_ptr<Page> makeForgePanel(NeuroforgeProcessor& p)
{
    auto root = std::make_unique<Page>();
    PassColumn* cols[3];
    for (int i = 0; i < 3; ++i) cols[i] = root->make<PassColumn>(p, i);
    Page* rp = root.get();
    root->painter = [rp](juce::Graphics& g) { paintPanel(g, rp->getLocalBounds()); };
    root->layout = [cols](juce::Rectangle<int> r) {
        r = r.reduced(8, 8);
        const int gap = 8, w = (r.getWidth() - 2 * gap) / 3;
        for (int i = 0; i < 3; ++i) cols[i]->setBounds(r.getX() + i * (w + gap), r.getY(), w, r.getHeight());
    };
    return root;
}

//==============================================================================================
// MACROS
//==============================================================================================
std::unique_ptr<Page> makeMacroPanel(NeuroforgeProcessor& p)
{
    static const char* names[] = { "GROWL", "TALK", "GRIT", "METAL", "MOTION", "WIDTH", "SUB", "TONE" };
    auto root = std::make_unique<Page>();
    std::vector<juce::Component*> chips, knobs;
    for (int i = 0; i < pid::kNumMacros; ++i) {
        chips.push_back(root->make<ModChip>(p, (int) ModSrc::Macro1 + i, names[i]));
        auto* k = root->make<Knob>(p, pid::macro(i), "");
        k->setLabel(juce::String("Macro ") + juce::String(i + 1));
        knobs.push_back(k);
    }
    Page* rp = root.get();
    root->painter = [rp](juce::Graphics& g) { paintPanel(g, rp->getLocalBounds(), "MACROS"); };
    root->layout = [=](juce::Rectangle<int> r) {
        r = r.reduced(8, 0).withTrimmedTop(22).withTrimmedBottom(6);
        const int cw = r.getWidth() / 4, ch = r.getHeight() / 2;
        for (int i = 0; i < 8; ++i) {
            const int x = r.getX() + (i % 4) * cw, y = r.getY() + (i / 4) * ch;
            chips[(size_t) i]->setBounds(x + 4, y, cw - 8, 16);
            knobs[(size_t) i]->setBounds(x, y + 17, cw, ch - 17);
        }
    };
    return root;
}

//==============================================================================================
// MOD area
//==============================================================================================
namespace {
struct DstGroup { juce::String name; std::vector<std::pair<int, juce::String>> items; };

const std::vector<DstGroup>& dstGroups()
{
    static const std::vector<DstGroup> groups = [] {
        std::vector<DstGroup> out;
        const auto& dst = modDestinations();
        const auto& defs = paramDefs();
        auto find = [&](const juce::String& n) -> DstGroup& {
            for (auto& g : out) if (g.name == n) return g;
            out.push_back({ n, {} });
            return out.back();
        };
        for (int i = 1; i < (int) dst.size(); ++i) {
            const juce::String n(defs[(size_t) dst[(size_t) i]].name);
            auto t = juce::StringArray::fromTokens(n, " ", "");
            juce::String group, item;
            const bool two = t.size() >= 3 && (t[0] == "Osc" || t[0] == "Slot" || t[0] == "Motion" || t[0] == "Env" ||
                                               t[0] == "LFO" || t[0] == "Mod" || t[0] == "Pass");
            if (two) { group = t[0] + " " + t[1]; item = juce::StringArray(t.begin() + 2, t.size() - 2).joinIntoString(" "); }
            else if (t.size() >= 2) { group = t[0]; item = juce::StringArray(t.begin() + 1, t.size() - 1).joinIntoString(" "); }
            else { group = "Global"; item = n; }
            find(group).items.push_back({ i, item });
        }
        return out;
    }();
    return groups;
}

class DstButton : public juce::TextButton {
public:
    DstButton(NeuroforgeProcessor& p, int dstParam) : pid_(dstParam)
    {
        att = std::make_unique<juce::ParameterAttachment>(*p.getParam(pid_), [this](float v) {
            const auto& d = paramDefs()[(size_t) pid_];
            const int i = (int) std::lround(normToReal(d, v));
            index = i;
            setButtonText(i <= 0 || i >= (int) d.choices.size() ? juce::String("-") : juce::String(d.choices[(size_t) i]));
        });
        att->sendInitialUpdate();
        onClick = [this] {
            juce::PopupMenu m;
            m.addItem(1, "None");
            m.addSeparator();
            for (auto& g : dstGroups()) {
                juce::PopupMenu sub;
                for (auto& it : g.items) sub.addItem(it.first + 1, it.second, true, it.first == index);
                m.addSubMenu(g.name, sub);
            }
            juce::Component::SafePointer<DstButton> sp(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMaximumNumColumns(1), [sp](int r) {
                if (sp == nullptr || r == 0) return;
                sp->att->setValueAsCompleteGesture(realToNorm(paramDefs()[(size_t) sp->pid_], (float) (r - 1)));
            });
        };
    }
private:
    int pid_, index = 0;
    std::unique_ptr<juce::ParameterAttachment> att;
};

std::unique_ptr<Page> makeMatrixRow(NeuroforgeProcessor& p, int x)
{
    auto row = std::make_unique<Page>();
    auto* num = row->make<juce::Label>("", juce::String(x + 1));
    num->setFont(font(10.f));
    num->setColour(juce::Label::textColourId, cDim);
    num->setJustificationType(juce::Justification::centredRight);
    auto* src = row->make<ParamCombo>(p, pid::mx(x, pid::mxSrc));
    auto* dst = row->make<DstButton>(p, pid::mx(x, pid::mxDst));
    auto* amt = row->make<ParamBar>(p, pid::mx(x, pid::mxAmt), "");
    auto* clr = row->make<juce::TextButton>("x");
    clr->setTooltip("Clear this route");
    clr->onClick = [&p, x] { p.removeRoute(x); };
    row->layout = [=](juce::Rectangle<int> r) {
        r = r.reduced(2, 2);
        num->setBounds(r.removeFromLeft(18));
        r.removeFromLeft(3);
        src->setBounds(r.removeFromLeft(84));
        r.removeFromLeft(3);
        dst->setBounds(r.removeFromLeft(130));
        r.removeFromLeft(3);
        clr->setBounds(r.removeFromRight(20));
        r.removeFromRight(3);
        amt->setBounds(r);
    };
    return row;
}
} // namespace

std::unique_ptr<Page> makeModPanel(NeuroforgeProcessor& p)
{
    auto root = std::make_unique<Page>();
    auto* tabs = root->make<TabHost>(24, 84);

    // ---- envelopes ----
    {
        Page* pg = tabs->addTab("ENVELOPES");
        std::vector<Page*> cols;
        for (int e = 0; e < pid::kNumEnvs; ++e) {
            auto* c = pg->make<Page>();
            auto* loop = c->make<ParamToggle>(p, pid::env(e, pid::eLoop), "Loop");
            auto* view = c->make<EnvView>(p, e);
            std::vector<juce::Component*> ks;
            const char* ln[] = { "Attack", "Decay", "Sustain", "Release" };
            for (int k = 0; k < 4; ++k) ks.push_back(c->make<Knob>(p, pid::env(e, k), ln[k]));
            c->painter = [e](juce::Graphics& g) {
                g.setColour(cText);
                g.setFont(font(11.f, true));
                g.drawText(e == 0 ? "ENV 1  (AMP)" : "ENV " + juce::String(e + 1), 4, 0, 100, 20, juce::Justification::centredLeft);
            };
            c->layout = [=](juce::Rectangle<int> r) {
                auto h = r.removeFromTop(20);
                loop->setBounds(h.removeFromRight(70));
                r.removeFromTop(2);
                view->setBounds(r.removeFromTop(54));
                r.removeFromTop(2);
                placeCells(ks, r, 4, r.getHeight());
            };
            cols.push_back(c);
        }
        pg->layout = [cols](juce::Rectangle<int> r) {
            const int w = r.getWidth() / 3;
            for (size_t i = 0; i < cols.size(); ++i) cols[i]->setBounds(r.getX() + (int) i * w + 4, r.getY(), w - 8, r.getHeight());
        };
    }
    // ---- LFOs ----
    {
        Page* pg = tabs->addTab("LFOS");
        std::vector<Page*> cols;
        for (int l = 0; l < pid::kNumLfos; ++l) {
            auto* c = pg->make<Page>();
            auto* sync = c->make<ParamToggle>(p, pid::lfo(l, pid::lSync), "Sync");
            auto* retrig = c->make<ParamToggle>(p, pid::lfo(l, pid::lRetrig), "Retrig");
            auto* shape = c->make<ParamCombo>(p, pid::lfo(l, pid::lShape));
            auto* div = c->make<ParamCombo>(p, pid::lfo(l, pid::lDiv));
            auto* rate = c->make<ParamBar>(p, pid::lfo(l, pid::lRate), "Rate");
            auto* phase = c->make<ParamBar>(p, pid::lfo(l, pid::lPhase), "Phase");
            auto* view = c->make<TraceView>(p, [&p, l] { return p.getEngine().uiLfo[l].load(std::memory_order_relaxed); },
                                            cCyan, pid::lfo(l, pid::lShape));
            auto synced = std::make_shared<bool>(true);
            auto relayout = [c] { c->resized(); };
            sync->onToggled = [synced, relayout](bool b) { *synced = b; relayout(); };
            *synced = sync->getToggleState();
            c->painter = [l](juce::Graphics& g) {
                g.setColour(cText);
                g.setFont(font(11.f, true));
                g.drawText("LFO " + juce::String(l + 1), 4, 0, 50, 20, juce::Justification::centredLeft);
            };
            c->layout = [=](juce::Rectangle<int> r) {
                auto h = r.removeFromTop(20);
                h.removeFromLeft(46);
                sync->setBounds(h.removeFromLeft(58));
                retrig->setBounds(h);
                r.removeFromTop(3);
                auto row = r.removeFromTop(22);
                shape->setBounds(row.removeFromLeft(92));
                row.removeFromLeft(3);
                div->setVisible(*synced);
                rate->setVisible(!*synced);
                div->setBounds(row);
                rate->setBounds(row);
                r.removeFromTop(3);
                phase->setBounds(r.removeFromBottom(18));
                r.removeFromBottom(3);
                view->setBounds(r);
            };
            cols.push_back(c);
        }
        pg->layout = [cols](juce::Rectangle<int> r) {
            const int w = r.getWidth() / 4;
            for (size_t i = 0; i < cols.size(); ++i) cols[i]->setBounds(r.getX() + (int) i * w + 4, r.getY(), w - 8, r.getHeight());
        };
    }
    // ---- matrix ----
    {
        Page* pg = tabs->addTab("MATRIX");
        auto* vp = pg->make<juce::Viewport>();
        auto* list = pg->make<Page>();
        std::vector<Page*> rows;
        for (int x = 0; x < pid::kNumMatrix; ++x) {
            auto up = makeMatrixRow(p, x);
            rows.push_back(up.get());
            list->owned.add(up.release());
            list->addAndMakeVisible(rows.back());
        }
        list->layout = [rows](juce::Rectangle<int> r) {
            const int cw = r.getWidth() / 2, rh = 26;
            for (size_t i = 0; i < rows.size(); ++i)
                rows[i]->setBounds(r.getX() + ((int) i / 12) * cw, r.getY() + ((int) i % 12) * rh, cw, rh);
        };
        vp->setViewedComponent(list, false);
        vp->setScrollBarsShown(true, false);
        pg->layout = [vp, list](juce::Rectangle<int> r) {
            vp->setBounds(r);
            list->setSize(r.getWidth() - 10, 12 * 26 + 4);
        };
    }

    // ---- persistent source strip ----
    std::vector<juce::Component*> chips;
    for (int s = 1; s < (int) ModSrc::NumSources; ++s)
        if (s < (int) ModSrc::Macro1 || s > (int) ModSrc::Macro8) chips.push_back(root->make<ModChip>(p, s));
    Page* rp = root.get();
    root->painter = [rp](juce::Graphics& g) { paintPanel(g, rp->getLocalBounds()); };
    root->layout = [=](juce::Rectangle<int> r) {
        r = r.reduced(8, 6);
        tabs->setBounds(r);
        auto strip = r.removeFromTop(24).withTrimmedLeft(3 * 88 + 8);
        const int cw = strip.getWidth() / (int) chips.size();
        for (size_t i = 0; i < chips.size(); ++i)
            chips[i]->setBounds(strip.getX() + (int) i * cw + 1, strip.getY() + 1, cw - 2, 22);
    };
    return root;
}

//==============================================================================================
// OUTPUT
//==============================================================================================
std::unique_ptr<Page> makeOutputPanel(NeuroforgeProcessor& p)
{
    using namespace pid;
    auto root = std::make_unique<Page>();
    auto* guard = root->make<ParamToggle>(p, guardOn, "Low Guard");
    std::vector<juce::Component*> ks {
        root->make<Knob>(p, guardFreq, "Guard Freq"), root->make<Knob>(p, subDrive, "Sub Warmth"),
        root->make<Knob>(p, subLevel, "Sub Level"), root->make<Knob>(p, midLevel, "Mid Level"),
        root->make<Knob>(p, midWidth, "Mid Width"), root->make<Knob>(p, masterClip, "Master Clip") };
    auto* scope = root->make<ScopeView>(p);
    auto* spec = root->make<SpectrumView>(p);
    auto* kb = root->make<juce::MidiKeyboardComponent>(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    kb->setKeyWidth(14.f);
    kb->setAvailableRange(24, 96);
    kb->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffc9ced6));
    kb->setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff15181d));
    kb->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, cEdge);
    kb->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cAccent.withAlpha(0.3f));
    kb->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, cAccent.withAlpha(0.7f));
    kb->setScrollButtonsVisible(false);
    kb->setWantsKeyboardFocus(false);
    Page* rp = root.get();
    root->painter = [rp](juce::Graphics& g) { paintPanel(g, rp->getLocalBounds(), "OUTPUT"); };
    root->layout = [=](juce::Rectangle<int> r) {
        r = r.reduced(8, 0).withTrimmedTop(22).withTrimmedBottom(8);
        guard->setBounds(r.removeFromTop(24).removeFromLeft(110));
        placeCells(ks, r.removeFromTop(160), 3, 78, 2);
        r.removeFromTop(6);
        scope->setBounds(r.removeFromTop(96));
        r.removeFromTop(6);
        kb->setBounds(r.removeFromBottom(64));
        r.removeFromBottom(6);
        spec->setBounds(r);
    };
    return root;
}

//==============================================================================================
// TOP BAR
//==============================================================================================
namespace {
class CaptureSettingsPanel : public juce::Component {
public:
    explicit CaptureSettingsPanel(NeuroforgeProcessor& p) : proc(p)
    {
        auto setup = [this](Bar& b, const juce::String& lbl, float v, std::function<juce::String(float)> f, std::function<void(float)> apply) {
            b.label = lbl; b.fmt = std::move(f); b.setValue(v); b.defaultValue = v;
            b.onUserChange = std::move(apply);
            addAndMakeVisible(b);
        };
        auto& cs = proc.captureSettings;
        setup(note, "Note", (float) (cs.note - 24) / 48.f,
              [](float v) { return juce::MidiMessage::getMidiNoteName(24 + (int) std::lround(v * 48.f), true, true, 3); },
              [this](float v) { proc.captureSettings.note = 24 + (int) std::lround(v * 48.f); });
        setup(beats, "Beats", (float) (cs.beats - 1.0) / 15.f,
              [](float v) { return juce::String(1 + (int) std::lround(v * 15.f)); },
              [this](float v) { proc.captureSettings.beats = 1 + (int) std::lround(v * 15.f); });
        setup(frames, "Frames", (float) (cs.maxFrames - 16) / 240.f,
              [](float v) { return juce::String(16 + (int) std::lround(v * 240.f)); },
              [this](float v) { proc.captureSettings.maxFrames = 16 + (int) std::lround(v * 240.f); });
        setSize(220, 100);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(cPanel);
        g.setColour(cDim);
        g.setFont(font(10.f));
        g.drawText("Capture length uses host BPM when available.", 8, 78, 204, 16, juce::Justification::centredLeft);
    }
    void resized() override
    {
        note.setBounds(10, 10, 200, 20);
        beats.setBounds(10, 34, 200, 20);
        frames.setBounds(10, 58, 200, 20);
    }
private:
    NeuroforgeProcessor& proc;
    Bar note, beats, frames;
};
} // namespace

TopBar::TopBar(NeuroforgeProcessor& p) : proc(p)
{
    prev = make<juce::TextButton>("<");
    next = make<juce::TextButton>(">");
    presetBtn = make<juce::TextButton>(p.presetName);
    saveBtn = make<juce::TextButton>("Save");
    archetype = make<juce::ComboBox>();
    forge = make<juce::TextButton>("FORGE");
    mutate = make<juce::TextButton>("MUTATE");
    mutAmt = make<Bar>();
    capture = make<juce::TextButton>("CAPTURE");
    nextGen = make<juce::TextButton>("NEXT GEN");
    capSet = make<juce::TextButton>("...");
    status = make<juce::Label>();
    auto* quality = make<ParamCombo>(p, pid::quality);
    auto* master = make<Knob>(p, pid::masterGain, "Master");
    auto* meter = make<Meter>(p);

    int i = 1;
    for (auto& n : archetypeNames()) archetype->addItem(juce::String(n), i++);
    archetype->setSelectedId(1, juce::dontSendNotification);
    archetype->setTooltip("Archetype used by FORGE");

    forge->setColour(juce::TextButton::buttonColourId, cAccent);
    forge->setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    forge->setTooltip("Generate a new patch from the selected archetype with a random seed");
    forge->onClick = [this] {
        proc.generate(archetype->getSelectedId() - 1, (uint32_t) juce::Random::getSystemRandom().nextInt());
    };
    mutate->setColour(juce::TextButton::buttonColourId, cPanel2);
    mutate->setColour(juce::TextButton::textColourOffId, cCyan);
    mutate->setTooltip("Nudge the current patch (subtle .. wild)");
    mutate->onClick = [this] { proc.mutate(mutateAmount); };
    mutAmt->label = "Amount";
    mutAmt->defaultValue = 0.4f;
    mutAmt->setValue(0.4f);
    mutAmt->fmt = [](float v) { return v < 0.25f ? juce::String("subtle") : (v < 0.6f ? juce::String("medium") : juce::String("wild")); };
    mutAmt->onUserChange = [this](float v) { mutateAmount = v; };

    capture->setColour(juce::TextButton::textColourOffId, cAccent);
    capture->setTooltip("Render the current patch offline into a wavetable slot (resample)");
    capture->onClick = [this] { proc.startCapture(false); };
    nextGen->setColour(juce::TextButton::textColourOffId, cAccent);
    nextGen->setTooltip("Capture, then load a fresh patch that plays the capture on Osc A (re-process it)");
    nextGen->onClick = [this] { proc.startCapture(true); };
    capSet->setTooltip("Capture settings");
    capSet->onClick = [this] { showCaptureSettings(); };

    status->setFont(font(10.5f));
    status->setColour(juce::Label::textColourId, cDim);
    status->setJustificationType(juce::Justification::centredLeft);

    prev->onClick = [this] { step(-1); };
    next->onClick = [this] { step(1); };
    presetBtn->onClick = [this] { showPresetMenu(); };
    presetBtn->setColour(juce::TextButton::buttonColourId, cPanel2);
    saveBtn->onClick = [this] { showSaveDialog(); };

    painter = [this](juce::Graphics& g) {
        paintPanel(g, getLocalBounds());
        if (logoImage().isValid())
            g.drawImageWithin(logoImage(), 12, 2, 158, getHeight() - 4, juce::RectanglePlacement::centred);
    };
    layout = [=](juce::Rectangle<int>) {
        const int y1 = 6, h1 = 22;
        prev->setBounds(176, y1, 22, h1);
        presetBtn->setBounds(200, y1, 190, h1);
        next->setBounds(392, y1, 22, h1);
        saveBtn->setBounds(418, y1, 46, h1);
        archetype->setBounds(484, y1, 104, h1);
        forge->setBounds(592, y1, 74, 34);
        mutate->setBounds(690, y1, 70, h1);
        mutAmt->setBounds(690, 30, 120, 12);
        capture->setBounds(830, y1, 76, h1);
        nextGen->setBounds(910, y1, 80, h1);
        capSet->setBounds(994, y1, 26, h1);
        status->setBounds(830, 29, 190, 14);
        quality->setBounds(1040, 12, 58, 22);
        master->setBounds(1108, 2, 44, 42);
        meter->setBounds(1166, 14, 84, 18);
    };
}

void TopBar::tick()
{
    if (!isShowing()) return;
    if (proc.presetName != lastName) { lastName = proc.presetName; presetBtn->setButtonText(lastName); }
    const bool busy = proc.isCapturing();
    const auto st = proc.getCaptureStatus();
    if (st != lastStatus) { lastStatus = st; status->setText(st, juce::dontSendNotification); }
    if (busy != lastBusy) {
        lastBusy = busy;
        capture->setEnabled(!busy);
        nextGen->setEnabled(!busy);
    }
}

void TopBar::step(int dir)
{
    presetCache = proc.getPresetList();
    if (presetCache.empty()) return;
    const juce::String cur = proc.presetName.trimCharactersAtEnd("*");
    int idx = -1;
    for (int i = 0; i < (int) presetCache.size(); ++i) if (presetCache[(size_t) i].name == cur) { idx = i; break; }
    const int n = (int) presetCache.size();
    idx = idx < 0 ? (dir > 0 ? 0 : n - 1) : (idx + dir + n) % n;
    proc.loadPreset(presetCache[(size_t) idx]);
}

void TopBar::showPresetMenu()
{
    presetCache = proc.getPresetList();
    juce::PopupMenu m;
    std::vector<juce::String> cats;
    for (auto& e : presetCache) if (!e.user && std::find(cats.begin(), cats.end(), e.category) == cats.end()) cats.push_back(e.category);
    if (cats.empty()) m.addItem(-1, "(no factory presets)", false);
    for (auto& c : cats) {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) presetCache.size(); ++i)
            if (!presetCache[(size_t) i].user && presetCache[(size_t) i].category == c) sub.addItem(1000 + i, presetCache[(size_t) i].name);
        m.addSubMenu(c, sub);
    }
    bool anyUser = false;
    juce::PopupMenu user;
    for (int i = 0; i < (int) presetCache.size(); ++i)
        if (presetCache[(size_t) i].user) { user.addItem(1000 + i, presetCache[(size_t) i].name); anyUser = true; }
    m.addSubMenu("User", user, anyUser);
    m.addSeparator();
    m.addItem(1, "Save As...");
    m.addItem(2, "Init");
    juce::Component::SafePointer<TopBar> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetBtn), [sp](int r) {
        if (sp == nullptr || r <= 0) return;
        if (r == 1) sp->showSaveDialog();
        else if (r == 2) sp->proc.initPatch();
        else if (r >= 1000 && r - 1000 < (int) sp->presetCache.size()) sp->proc.loadPreset(sp->presetCache[(size_t) (r - 1000)]);
    });
}

void TopBar::showSaveDialog()
{
    auto* aw = new juce::AlertWindow("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor("name", proc.presetName.trimCharactersAtEnd("*"), "");
    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    NeuroforgeProcessor* pp = &proc;
    aw->enterModalState(true, juce::ModalCallbackFunction::create([pp, aw](int r) {
        if (r == 1) pp->saveUserPreset(aw->getTextEditorContents("name"));
    }), true);
}

void TopBar::showCaptureSettings()
{
    auto content = std::make_unique<CaptureSettingsPanel>(proc);
    juce::CallOutBox::launchAsynchronously(std::move(content), capSet->getScreenBounds(), nullptr);
}

} // namespace nfui
