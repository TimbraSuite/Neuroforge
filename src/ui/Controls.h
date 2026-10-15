#pragma once
#include "../PluginProcessor.h"
#include "Look.h"
#include <functional>

namespace nfui {

// Anything that wants a 30 Hz refresh registers here; the editor owns the single timer.
struct Tickable {
    Tickable() { all().push_back(this); }
    virtual ~Tickable() { auto& v = all(); v.erase(std::remove(v.begin(), v.end(), this), v.end()); }
    virtual void tick() = 0;
    static std::vector<Tickable*>& all() { static std::vector<Tickable*> v; return v; }
    static void tickAll() { for (auto* t : all()) t->tick(); }
};

const char* modSrcShort(int src);
juce::Colour modSrcColour(int src);

//==============================================================================================
// The modulation source chip currently under the mouse (0 = none); knobs it targets highlight.
int& hoveredModSource();

class Knob : public juce::Component, public juce::SettableTooltipClient, public juce::DragAndDropTarget, public Tickable {
public:
    Knob(NeuroforgeProcessor& p, int paramId, juce::String label = {});
    void setLabel(const juce::String& s) { label = s; repaint(); }
    void setFormatter(std::function<juce::String(float)> f) { fmt = std::move(f); repaint(); }
    void setBipolar(bool b) { bipolar = b; repaint(); }
    // Inline layout: small circle on the left, "label  value" and badges to its right (compact footers).
    void setInline(bool b) { inlineMode = b; repaint(); }
    int getParamId() const { return pid; }
    float getValue() const { return value; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; hoverBadge = -1; overCircle = false; repaint(); }
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void enablementChanged() override { repaint(); }
    void tick() override;
    void enterValue();   // typed value entry (right-click menu / Enter key)
    void showRoutesMenu(); // popover with every modulation route on this knob

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped(const SourceDetails&) override;

private:
    void showMenu();
    void showBadgeMenu(int badge);
    enum class Size { Small, Medium, Large };   // same design at every size; proportions scale with the radius
    Size sizeClass() const;
    juce::Rectangle<float> circleArea() const;
    bool hasBadgeRow() const;
    juce::Rectangle<float> badgeArea() const;
    juce::Rectangle<float> badgeRect(int i) const;
    int badgeSlots() const;
    int shownBadges() const;
    bool hasOverflow() const;
    int badgeAt(juce::Point<float>) const;
    NeuroforgeProcessor& proc;
    int pid;
    juce::String label;
    std::function<juce::String(float)> fmt;
    std::unique_ptr<juce::ParameterAttachment> att;
    float value = 0.f, defNorm = 0.f, modOff = 0.f, dragStartVal = 0.f;
    int dragStartY = 0, routeCount = 0, altRow = -1, hoverBadge = -1, badgeDrag = -1;
    std::vector<NfModRoute> routes;   // modulation routes targeting this knob (refreshed in tick)
    bool overCircle = false, inlineMode = false;
    int dragHintSrc = 0, lastHoverSrc = 0;   // drag-to-modulate target hint / hovered source chip
    float flash = 0.f;                       // fades after a route is assigned
    bool bipolar = false, hover = false, dragging = false, dropHighlight = false, modulatable = false, discrete = false;
};

//==============================================================================================
class ParamCombo : public juce::ComboBox {
public:
    ParamCombo(NeuroforgeProcessor& p, int paramId);
    std::function<void(int)> onIndexChanged;
    int getIndex() const { return index; }
private:
    int pid, index = 0;
    bool updating = false;
    std::unique_ptr<juce::ParameterAttachment> att;
};

class ParamToggle : public juce::ToggleButton {
public:
    ParamToggle(NeuroforgeProcessor& p, int paramId, const juce::String& text);
    std::function<void(bool)> onToggled;
private:
    int pid;
    std::unique_ptr<juce::ParameterAttachment> att;
};

//==============================================================================================
// Horizontal value bar (relative drag). Used for rates, mod amounts, mutate amount.
class Bar : public juce::Component, public juce::SettableTooltipClient {
public:
    float value = 0.f, defaultValue = 0.f;
    bool bipolar = false;
    juce::String label;
    juce::Colour fill = tok::accent;   // bar colour (modulation depth bars use their source colour)
    std::function<juce::String(float)> fmt;
    std::function<void()> onBegin, onEnd;
    std::function<void(float)> onUserChange;

    void setValue(float v) { v = juce::jlimit(0.f, 1.f, v); if (v != value) { value = v; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    float startVal = 0.f;
};

class ParamBar : public Bar {
public:
    ParamBar(NeuroforgeProcessor& p, int paramId, const juce::String& lbl);
private:
    std::unique_ptr<juce::ParameterAttachment> att;
};

//==============================================================================================
class ModChip : public juce::Component, public juce::SettableTooltipClient, public Tickable {
public:
    ModChip(NeuroforgeProcessor& p, int src, juce::String text = {});
    int getSource() const { return src; }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hoveredModSource() = src; }
    void mouseExit(const juce::MouseEvent&) override { if (hoveredModSource() == src) hoveredModSource() = 0; }
    void tick() override;
private:
    NeuroforgeProcessor& proc;
    int src;
    juce::String text;
    float level = 0.f;
};

} // namespace nfui
