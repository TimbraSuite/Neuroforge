#include "Controls.h"
#include <cmath>

using namespace nf;

namespace nfui {

const char* modSrcShort(int s)
{
    static const char* n[] = { "", "ENV 1", "ENV 2", "ENV 3", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "MOT 1", "MOT 2",
                               "MOT 3", "M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8", "VEL", "NOTE", "MW", "PB",
                               "AT", "RND", "STP 1", "STP 2" };
    return (s >= 0 && s < (int) (sizeof(n) / sizeof(n[0]))) ? n[s] : "";
}

juce::Colour modSrcColour(int s)
{
    if (s >= 1 && s <= 3) return juce::Colour(0xffffb23c);
    if (s >= 4 && s <= 7) return cCyan;
    if (s >= 8 && s <= 10) return juce::Colour(0xffc58cff);
    if (s >= 11 && s <= 18) return cAccent;
    if (s == (int) ModSrc::Step1 || s == (int) ModSrc::Step2) return juce::Colour(0xffff5fa2);
    return juce::Colour(0xff9aa6b4);
}

//==============================================================================================
Knob::Knob(NeuroforgeProcessor& p, int paramId, juce::String lbl)
    : proc(p), pid(paramId), label(std::move(lbl))
{
    const auto& d = paramDefs()[(size_t) pid];
    if (label.isEmpty()) {
        // strip common prefixes from the long display name
        juce::String n(d.name);
        for (auto* pre : { "Osc A ", "Osc B ", "FM Op ", "Sub ", "Filter ", "Env 1 ", "Env 2 ", "Env 3 ", "LFO 1 ", "LFO 2 ",
                           "LFO 3 ", "LFO 4 ", "Motion 1 ", "Motion 2 ", "Motion 3 " })
            if (n.startsWith(pre)) { n = n.substring((int) std::strlen(pre)); break; }
        label = n;
    }
    bipolar = d.min < 0.f && d.max > 0.f && d.curve != Curve::Log;
    modulatable = d.modulatable;
    discrete = d.curve == Curve::Disc || d.curve == Curve::Toggle;
    defNorm = realToNorm(d, d.def);
    fmt = [this](float v) { return nfFormat(pid, v); };
    setTooltip(juce::String(d.name) + "  (drag; ctrl = fine; dbl-click = reset; alt-drag = mod amount; right-click = menu)");
    setRepaintsOnMouseActivity(false);
    att = std::make_unique<juce::ParameterAttachment>(*proc.getParam(pid), [this](float v) { value = v; repaint(); });
    att->sendInitialUpdate();
}

static bool srcIsBipolar(int s)
{
    switch ((ModSrc) s) {
        case ModSrc::Lfo1: case ModSrc::Lfo2: case ModSrc::Lfo3: case ModSrc::Lfo4:
        case ModSrc::Motion1: case ModSrc::Motion2: case ModSrc::Motion3:
        case ModSrc::Note: case ModSrc::PitchBend: case ModSrc::Random: return true;
        default: return false;
    }
}

// Very short source names for the badges under a knob.
static juce::String badgeText(int s)
{
    if (s >= (int) ModSrc::Env1 && s <= (int) ModSrc::Env3) return "E" + juce::String(s - (int) ModSrc::Env1 + 1);
    if (s >= (int) ModSrc::Lfo1 && s <= (int) ModSrc::Lfo4) return "L" + juce::String(s - (int) ModSrc::Lfo1 + 1);
    if (s >= (int) ModSrc::Motion1 && s <= (int) ModSrc::Motion3) return "MO" + juce::String(s - (int) ModSrc::Motion1 + 1);
    if (s >= (int) ModSrc::Macro1 && s <= (int) ModSrc::Macro8) return "M" + juce::String(s - (int) ModSrc::Macro1 + 1);
    if (s == (int) ModSrc::Step1 || s == (int) ModSrc::Step2) return "S" + juce::String(s - (int) ModSrc::Step1 + 1);
    switch ((ModSrc) s) {
        case ModSrc::Velocity: return "VL"; case ModSrc::Note: return "NT"; case ModSrc::ModWheel: return "MW";
        case ModSrc::PitchBend: return "PB"; case ModSrc::Aftertouch: return "AT"; case ModSrc::Random: return "RN";
        default: return "?";
    }
}

bool Knob::hasBadgeRow() const { return modulatable && getHeight() >= 56; }

juce::Rectangle<float> Knob::badgeArea() const
{
    const float labelH = 12.f, badgeH = 11.f;
    auto b = getLocalBounds().toFloat();
    if (hasBadgeRow()) return { b.getX(), b.getBottom() - labelH - badgeH, b.getWidth(), badgeH };
    return { b.getX(), b.getBottom() - labelH, b.getWidth(), labelH }; // small knobs: badges share the label row
}

juce::Rectangle<float> Knob::badgeRect(int i) const
{
    const int n = (int) routes.size();
    auto area = badgeArea();
    const float gap = 2.f;
    const float w = juce::jmin(24.f, (area.getWidth() - gap * (float) (n - 1)) / (float) juce::jmax(1, n));
    const float total = w * (float) n + gap * (float) (n - 1);
    return { area.getCentreX() - total * 0.5f + (float) i * (w + gap), area.getY() + 0.5f, w, area.getHeight() - 1.f };
}

int Knob::badgeAt(juce::Point<float> pt) const
{
    if (routes.empty()) return -1;
    if (!hasBadgeRow() && overCircle) return -1; // small knob, label showing
    for (int i = 0; i < (int) routes.size(); ++i)
        if (badgeRect(i).expanded(1.f, 2.f).contains(pt)) return i;
    return -1;
}

void Knob::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float labelH = 12.f;
    auto area = b.withTrimmedBottom(labelH + (hasBadgeRow() ? 11.f : 0.f));
    const float R = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 1.5f;
    const auto c = area.getCentre();
    const float rk = R - 4.f;
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    auto ang = [&](float v) { return a0 + (a1 - a0) * juce::jlimit(0.f, 1.f, v); };

    if (dropHighlight) {
        g.setColour(cCyan.withAlpha(0.25f));
        g.fillEllipse(c.x - R - 1.f, c.y - R - 1.f, 2.f * R + 2.f, 2.f * R + 2.f);
        g.setColour(cCyan);
        g.drawEllipse(c.x - R - 1.f, c.y - R - 1.f, 2.f * R + 2.f, 2.f * R + 2.f, 1.5f);
    }

    const juce::PathStrokeType stroke(rk > 14.f ? 3.2f : 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track;
    track.addCentredArc(c.x, c.y, rk, rk, 0.f, a0, a1, true);
    g.setColour(cTrack);
    g.strokePath(track, stroke);

    float from = bipolar ? ang(0.5f) : a0, to = ang(value);
    if (to < from) std::swap(from, to);
    if (to - from > 0.02f) {
        juce::Path va;
        va.addCentredArc(c.x, c.y, rk, rk, 0.f, from, to, true);
        g.setColour(cAccent);
        g.strokePath(va, stroke);
    }

    // one arc per modulation route, in its source colour, showing the range it can move the knob
    for (int i = 0; i < (int) routes.size() && i < 3; ++i) {
        const auto& rt = routes[(size_t) i];
        const float lo = srcIsBipolar(rt.src) ? value - std::abs(rt.amount) : juce::jmin(value, value + rt.amount);
        const float hi = srcIsBipolar(rt.src) ? value + std::abs(rt.amount) : juce::jmax(value, value + rt.amount);
        const float m0 = ang(lo), m1 = ang(hi);
        if (m1 - m0 < 0.01f) continue;
        juce::Path mp;
        const float mr = rk + 3.8f + 2.6f * (float) i;
        mp.addCentredArc(c.x, c.y, mr, mr, 0.f, m0, m1, true);
        const bool hot = i == hoverBadge || i == badgeDrag;
        g.setColour(modSrcColour(rt.src).withAlpha(hot ? 1.f : 0.8f));
        g.strokePath(mp, juce::PathStrokeType(hot ? 2.2f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = rk - 3.5f;
    g.setColour(cPanel2);
    g.fillEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f);
    g.setColour(cEdge);
    g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2.f, bodyR * 2.f, 1.f);
    const float pa = ang(value) - juce::MathConstants<float>::halfPi; // convert 12-o'clock-based to standard
    g.setColour(cText);
    g.drawLine(c.x + std::cos(pa) * bodyR * 0.35f, c.y + std::sin(pa) * bodyR * 0.35f,
               c.x + std::cos(pa) * (bodyR - 1.5f), c.y + std::sin(pa) * (bodyR - 1.5f), 2.f);

    // live modulated position: a small dot riding on the value track
    if (std::abs(modOff) > 0.002f) {
        const float la = ang(value + modOff) - juce::MathConstants<float>::halfPi;
        g.setColour(juce::Colours::white);
        g.fillEllipse(c.x + std::cos(la) * rk - 2.2f, c.y + std::sin(la) * rk - 2.2f, 4.4f, 4.4f);
    }

    // ---- badges ----
    const bool showBadges = !routes.empty() && (hasBadgeRow() || !overCircle || badgeDrag >= 0);
    if (showBadges) {
        for (int i = 0; i < (int) routes.size(); ++i) {
            const auto& rt = routes[(size_t) i];
            const auto br = badgeRect(i);
            const auto col = modSrcColour(rt.src);
            const bool hot = i == hoverBadge || i == badgeDrag;
            g.setColour(col.withAlpha(hot ? 0.45f : 0.22f));
            g.fillRoundedRectangle(br, 2.5f);
            // amount bar along the bottom edge (bipolar from the centre)
            const float mid = br.getCentreX(), x = mid + rt.amount * br.getWidth() * 0.5f;
            g.setColour(col);
            g.fillRect(juce::Rectangle<float>(juce::jmin(mid, x), br.getBottom() - 2.f, std::abs(x - mid), 2.f));
            g.drawRoundedRectangle(br, 2.5f, hot ? 1.2f : 0.8f);
            if (br.getWidth() >= 11.f) {
                g.setColour(hot ? juce::Colours::white : col.brighter(0.3f));
                g.setFont(font(8.5f, true));
                g.drawText(badgeText(rt.src), br.withTrimmedBottom(1.f).toNearestInt(), juce::Justification::centred, false);
            }
        }
    }

    // ---- label / value line ----
    const auto labelRect = juce::Rectangle<int>(0, getHeight() - (int) labelH, getWidth(), (int) labelH);
    if (badgeDrag >= 0 || hoverBadge >= 0) {
        const auto& rt = routes[(size_t) (badgeDrag >= 0 ? badgeDrag : hoverBadge)];
        if (hasBadgeRow() || badgeDrag >= 0) {
            g.setColour(modSrcColour(rt.src));
            g.setFont(font(10.f, true));
            const juce::String t = juce::String(modSrcShort(rt.src)) + " " + (rt.amount >= 0.f ? "+" : "") +
                                   juce::String(juce::roundToInt(rt.amount * 100.f)) + "%";
            if (hasBadgeRow()) g.drawFittedText(t, labelRect, juce::Justification::centred, 1, 0.7f);
        }
    } else if (!showBadges || hasBadgeRow()) {
        const bool showVal = hover || dragging;
        g.setColour(showVal ? cAccent : cDim);
        g.setFont(font(showVal ? 10.5f : 10.f, showVal));
        g.drawFittedText(showVal ? fmt(value) : label, labelRect, juce::Justification::centred, 1, 0.7f);
    }
}

void Knob::tick()
{
    if (!isShowing()) return;
    bool rep = false;
    if (modulatable) {
        const float o = proc.getEngine().uiOffsets[pid].load(std::memory_order_relaxed);
        if (std::abs(o - modOff) > 0.002f && (std::abs(o) > 0.002f || std::abs(modOff) > 0.002f)) { modOff = o; rep = true; }
        auto r = proc.getRoutesFor(pid);
        bool same = r.size() == routes.size();
        for (size_t i = 0; same && i < r.size(); ++i)
            same = r[i].row == routes[i].row && r[i].src == routes[i].src && std::abs(r[i].amount - routes[i].amount) < 1e-4f;
        if (!same) {
            routes = std::move(r);
            if (hoverBadge >= (int) routes.size()) hoverBadge = -1;
            if (badgeDrag >= (int) routes.size()) badgeDrag = -1;
            rep = true;
        }
    }
    if (rep) repaint();
}

void Knob::mouseMove(const juce::MouseEvent& e)
{
    const auto b = getLocalBounds().toFloat();
    const bool oc = e.position.y < b.getBottom() - 12.f - (hasBadgeRow() ? 11.f : 0.f);
    const int hb = badgeAt(e.position);
    if (oc != overCircle || hb != hoverBadge) { overCircle = oc; hoverBadge = hb; repaint(); }
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    const int bi = badgeAt(e.position);
    if (bi >= 0) {
        if (e.mods.isPopupMenu()) { showBadgeMenu(bi); return; }
        badgeDrag = bi;
        dragStartY = e.y;
        dragStartVal = routes[(size_t) bi].amount;
        repaint();
        return;
    }
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    dragging = true;
    dragStartY = e.y;
    dragStartVal = value;
    altRow = -1;
    if (e.mods.isAltDown() && modulatable && !routes.empty()) { altRow = routes[0].row; dragStartVal = routes[0].amount; }
    if (altRow < 0) att->beginGesture();
    repaint();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    const float dy = (float) (dragStartY - e.y);
    const float fine = (e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.2f : 1.f;
    if (badgeDrag >= 0) {
        const float a = juce::jlimit(-1.f, 1.f, dragStartVal + dy / 120.f * fine);
        proc.setRouteAmount(routes[(size_t) badgeDrag].row, a);
        routes[(size_t) badgeDrag].amount = a;
        repaint();
        return;
    }
    if (!dragging) return;
    if (altRow >= 0) {
        proc.setRouteAmount(altRow, juce::jlimit(-1.f, 1.f, dragStartVal + dy / 100.f));
        return;
    }
    const float sens = fine < 1.f ? 1.f / 1000.f : 1.f / 180.f;
    const float nv = juce::jlimit(0.f, 1.f, dragStartVal + dy * sens);
    att->setValueAsPartOfGesture(nv);
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (dragging && altRow < 0) att->endGesture();
    dragging = false;
    altRow = -1;
    badgeDrag = -1;
    repaint();
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int bi = badgeAt(e.position);
    if (bi >= 0) { proc.removeRoute(routes[(size_t) bi].row); return; } // double-click a badge removes that route
    att->setValueAsCompleteGesture(defNorm);
}

void Knob::showBadgeMenu(int bi)
{
    const auto rt = routes[(size_t) bi];
    juce::PopupMenu m;
    m.addSectionHeader(juce::String(modSrcShort(rt.src)) + "  " + juce::String(juce::roundToInt(rt.amount * 100.f)) + "%");
    m.addItem(1, "Invert amount");
    m.addItem(2, "Set to +50%");
    m.addItem(3, "Remove modulation");
    juce::Component::SafePointer<Knob> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp, rt](int r) {
        if (sp == nullptr) return;
        if (r == 1) sp->proc.setRouteAmount(rt.row, -rt.amount);
        else if (r == 2) sp->proc.setRouteAmount(rt.row, 0.5f);
        else if (r == 3) sp->proc.removeRoute(rt.row);
    });
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto& d = paramDefs()[(size_t) pid];
    const float steps = discrete ? std::max(1.f, d.max - d.min) : 50.f;
    const float step = (e.mods.isCtrlDown() || e.mods.isCommandDown() ? 0.25f : 1.f) / steps;
    const float dir = w.deltaY > 0 ? 1.f : (w.deltaY < 0 ? -1.f : 0.f);
    if (dir != 0.f) att->setValueAsCompleteGesture(juce::jlimit(0.f, 1.f, value + dir * step));
}

bool Knob::isInterestedInDragSource(const SourceDetails& d)
{
    return modulatable && d.description.toString().startsWith("mod:");
}

void Knob::itemDropped(const SourceDetails& d)
{
    dropHighlight = false;
    const int src = d.description.toString().fromFirstOccurrenceOf("mod:", false, false).getIntValue();
    proc.addRoute(src, pid, 0.5f);
    repaint();
}

void Knob::showMenu()
{
    juce::PopupMenu m;
    const auto routes = proc.getRoutesFor(pid);
    for (auto& r : routes)
        m.addItem(100 + r.row, juce::String("Remove modulation from ") + modSrcShort(r.src) +
                                   " (" + juce::String(r.amount, 2) + ")");
    if (!routes.empty()) m.addSeparator();
    m.addItem(1, "Reset to default");
    if (modulatable) {
        juce::PopupMenu mac;
        for (int i = 0; i < pid::kNumMacros; ++i)
            mac.addItem(200 + i, "Macro " + juce::String(i + 1) + " (" + modSrcShort((int) ModSrc::Macro1 + i) + ")");
        m.addSubMenu("Assign to Macro", mac);
    }
    juce::Component::SafePointer<Knob> sp(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp](int r) {
        if (sp == nullptr || r == 0) return;
        if (r == 1) sp->att->setValueAsCompleteGesture(sp->defNorm);
        else if (r >= 100 && r < 200) sp->proc.removeRoute(r - 100);
        else if (r >= 200 && r < 208) sp->proc.addRoute((int) ModSrc::Macro1 + (r - 200), sp->pid, 0.5f);
    });
}

//==============================================================================================
ParamCombo::ParamCombo(NeuroforgeProcessor& p, int paramId) : pid(paramId)
{
    const auto& d = paramDefs()[(size_t) pid];
    int i = 1;
    for (auto& c : d.choices) addItem(juce::String(c), i++);
    setJustificationType(juce::Justification::centredLeft);
    att = std::make_unique<juce::ParameterAttachment>(*p.getParam(pid), [this](float v) {
        const auto& dd = paramDefs()[(size_t) pid];
        index = (int) std::lround(normToReal(dd, v));
        updating = true;
        setSelectedItemIndex(index, juce::dontSendNotification);
        updating = false;
        if (onIndexChanged) onIndexChanged(index);
    });
    onChange = [this] {
        if (updating) return;
        att->setValueAsCompleteGesture(realToNorm(paramDefs()[(size_t) pid], (float) getSelectedItemIndex()));
    };
    att->sendInitialUpdate();
}

ParamToggle::ParamToggle(NeuroforgeProcessor& p, int paramId, const juce::String& text) : pid(paramId)
{
    setButtonText(text);
    att = std::make_unique<juce::ParameterAttachment>(*p.getParam(pid), [this](float v) {
        setToggleState(v >= 0.5f, juce::dontSendNotification);
        if (onToggled) onToggled(v >= 0.5f);
    });
    onClick = [this] { att->setValueAsCompleteGesture(getToggleState() ? 1.f : 0.f); };
    att->sendInitialUpdate();
}

//==============================================================================================
void Bar::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(cTrack);
    g.fillRoundedRectangle(r, 3.f);
    juce::Rectangle<float> f;
    if (bipolar) {
        const float mid = r.getX() + r.getWidth() * 0.5f, x = r.getX() + r.getWidth() * value;
        f = { std::min(mid, x), r.getY(), std::abs(x - mid), r.getHeight() };
    } else
        f = r.withWidth(r.getWidth() * value);
    g.setColour(cAccent.withAlpha(0.75f));
    g.fillRoundedRectangle(f, 3.f);
    g.setColour(cEdge);
    g.drawRoundedRectangle(r, 3.f, 1.f);
    g.setFont(font(10.5f));
    g.setColour(cText);
    auto tr = getLocalBounds().reduced(4, 0);
    if (label.isNotEmpty()) g.drawText(label, tr, juce::Justification::centredLeft);
    if (fmt) {
        g.setColour(bipolar || value > 0.5f ? juce::Colours::white : cText);
        g.drawText(fmt(value), tr, juce::Justification::centredRight);
    }
}

void Bar::mouseDown(const juce::MouseEvent&)
{
    startVal = value;
    if (onBegin) onBegin();
}

void Bar::mouseDrag(const juce::MouseEvent& e)
{
    const float sens = (e.mods.isCommandDown() || e.mods.isCtrlDown()) ? 0.15f : 1.f;
    const float nv = juce::jlimit(0.f, 1.f, startVal + sens * (float) e.getDistanceFromDragStartX() / (float) juce::jmax(20, getWidth() - 8));
    if (nv != value) { value = nv; repaint(); if (onUserChange) onUserChange(nv); }
}

void Bar::mouseUp(const juce::MouseEvent&) { if (onEnd) onEnd(); }

void Bar::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onBegin) onBegin();
    value = defaultValue; repaint();
    if (onUserChange) onUserChange(value);
    if (onEnd) onEnd();
}

ParamBar::ParamBar(NeuroforgeProcessor& p, int paramId, const juce::String& lbl)
{
    label = lbl;
    const auto& d = paramDefs()[(size_t) paramId];
    bipolar = d.min < 0.f && d.max > 0.f;
    defaultValue = realToNorm(d, d.def);
    fmt = [paramId](float v) { return nfFormat(paramId, v); };
    att = std::make_unique<juce::ParameterAttachment>(*p.getParam(paramId), [this](float v) { setValue(v); });
    onBegin = [this] { att->beginGesture(); };
    onUserChange = [this](float v) { att->setValueAsPartOfGesture(v); };
    onEnd = [this] { att->endGesture(); };
    att->sendInitialUpdate();
}

//==============================================================================================
ModChip::ModChip(NeuroforgeProcessor& p, int s, juce::String t) : proc(p), src(s), text(t.isEmpty() ? juce::String(modSrcShort(s)) : t)
{
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("Drag onto a knob to modulate it");
}

void ModChip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto col = modSrcColour(src);
    g.setColour(col.withAlpha(0.12f));
    g.fillRoundedRectangle(r, 4.f);
    g.setColour(col.withAlpha(0.8f));
    g.drawRoundedRectangle(r, 4.f, 1.f);
    if (level > 0.01f) {
        g.setColour(col.withAlpha(0.55f));
        g.fillRoundedRectangle(r.getX() + 2.f, r.getBottom() - 4.f, (r.getWidth() - 4.f) * juce::jlimit(0.f, 1.f, level), 2.f, 1.f);
    }
    g.setColour(col);
    g.setFont(font(10.f, true));
    g.drawFittedText(text, getLocalBounds().reduced(2, 0), juce::Justification::centred, 1, 0.7f);
}

void ModChip::tick()
{
    if (!isShowing()) return;
    auto& e = proc.getEngine();
    float v = 0.f;
    if (src >= 1 && src <= 3) v = e.uiEnv[src - 1].load(std::memory_order_relaxed);
    else if (src >= 4 && src <= 7) v = std::abs(e.uiLfo[src - 4].load(std::memory_order_relaxed));
    else if (src >= 8 && src <= 10) v = std::abs(e.uiMotion[src - 8].load(std::memory_order_relaxed));
    else if (src >= 11 && src <= 18) v = proc.getRaw(pid::macro(src - 11));
    else return;
    if (std::abs(v - level) > 0.01f) { level = v; repaint(); }
}

void ModChip::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 5) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
        if (!dc->isDragAndDropActive())
            dc->startDragging("mod:" + juce::String(src), this);
}

} // namespace nfui
