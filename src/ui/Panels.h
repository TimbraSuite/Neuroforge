#pragma once
#include "Controls.h"
#include "Displays.h"
#include <atomic>
#include <thread>

namespace nfui {

// Generic container that owns its children and lets the creator supply layout / paint lambdas.
class Page : public juce::Component {
public:
    std::function<void(juce::Rectangle<int>)> layout;
    std::function<void(juce::Graphics&)> painter;
    juce::OwnedArray<juce::Component> owned;

    template <class T, class... A>
    T* make(A&&... a)
    {
        auto* c = new T(std::forward<A>(a)...);
        owned.add(c);
        addAndMakeVisible(c);
        return c;
    }
    void resized() override { if (layout) layout(getLocalBounds()); }
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
};

// Row of tab buttons + a stack of pages.
class TabHost : public Page {
public:
    explicit TabHost(int tabBarHeight = 24, int tabWidth = 84) : barH(tabBarHeight), tabW(tabWidth) {}
    Page* addTab(const juce::String& name);
    void select(int i);
    void resized() override;
private:
    int barH, tabW, current = 0;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::OwnedArray<Page> pages;
};

class SlotStrip : public Page {
public:
    SlotStrip(NeuroforgeProcessor& p, int slotIndex);
    int getDesiredHeight() const;
    bool isEmpty() const { return off; }
    std::function<void()> onHeightChange;
    void resized() override;
private:
    void applyType(int type);
    NeuroforgeProcessor& proc;
    int slot;
    bool off = true;
    ParamCombo* type = nullptr;
    juce::TextButton *addBtn = nullptr, *remBtn = nullptr;
    Knob* pk[4] {};
    Knob *mix = nullptr, *mot = nullptr;
    int numUsed = 0;
};

class MotionEditor : public Page {
public:
    MotionEditor(NeuroforgeProcessor& p, int motionIndex);
    void resized() override;
private:
    bool stepsMode = false, synced = true;
    ParamCombo *mode = nullptr, *div = nullptr, *shape = nullptr;
    ParamToggle* sync = nullptr;
    ParamBar* rate = nullptr;
    TraceView* trace = nullptr;
    StepSeq* seq = nullptr;
    Knob *depth = nullptr, *steps = nullptr, *glide = nullptr;
};

class PassColumn : public Page {
public:
    PassColumn(NeuroforgeProcessor& p, int passIndex);
    void resized() override;
private:
    void updateActive(bool isOn);
    NeuroforgeProcessor& proc;
    int idx;
    bool active = true;
    ParamToggle* on = nullptr;
    ModChip* chip = nullptr;
    ParamBar* mixBar = nullptr;
    MotionEditor* motion = nullptr;
    juce::Viewport* chainView = nullptr;
    Page chain;                       // scrollable effect chain
    SlotStrip* slots[4] {};
    juce::Rectangle<int> motionPlate, chainLabel;
};

std::unique_ptr<Page> makeSourcePanel(NeuroforgeProcessor&);
std::unique_ptr<Page> makeForgePanel(NeuroforgeProcessor&);
std::unique_ptr<Page> makeMacroPanel(NeuroforgeProcessor&);
std::unique_ptr<Page> makeModPanel(NeuroforgeProcessor&);
std::unique_ptr<Page> makeOutputPanel(NeuroforgeProcessor&);

class TopBar : public Page, public Tickable {
public:
    explicit TopBar(NeuroforgeProcessor& p);
    ~TopBar() override;
    void tick() override;
    std::function<void()> onLogoClick; // About box
    void mouseDown(const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;
private:
    static juce::Rectangle<int> logoRect() { return { 12, 0, 158, 30 }; }
    void showPresetMenu();
    void step(int dir);
    void showSaveDialog();
    void showCaptureSettings();
    NeuroforgeProcessor& proc;
    juce::TextButton *prev = nullptr, *next = nullptr, *presetBtn = nullptr, *saveBtn = nullptr, *initBtn = nullptr, *forge = nullptr, *updateBtn = nullptr,
                     *mutate = nullptr, *undo = nullptr, *capture = nullptr, *nextGen = nullptr, *capSet = nullptr;
    juce::ComboBox* archetype = nullptr;
    Bar* mutAmt = nullptr;
    juce::Label* status = nullptr;
    std::vector<NfPresetEntry> presetCache;
    juce::String lastName, lastStatus;
    bool lastBusy = false, lastCanUndo = true;
    float mutateAmount = 0.4f;
    std::thread updateCheckThread;
    std::atomic<bool> updateAvailable { false };
    juce::String availableVersion, availableDownloadUrl;
};

} // namespace nfui
