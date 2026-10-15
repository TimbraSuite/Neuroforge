#pragma once
#include "PluginProcessor.h"
#include "ui/Panels.h"

class NeuroforgeEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         private juce::Timer {
public:
    explicit NeuroforgeEditor(NeuroforgeProcessor&);
    ~NeuroforgeEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1280, kH = 800;

private:
    void timerCallback() override;

    struct Content : juce::Component {
        void paint(juce::Graphics& g) override { g.fillAll(nfui::cBg); nfui::paintMetal(g, getLocalBounds().toFloat(), 0.f, 3, nfui::kMetalOpacity * 0.5f); }
    };

    NeuroforgeProcessor& proc;
    nfui::LookAndFeel laf;
    Content content;
    std::unique_ptr<nfui::TopBar> top;
    std::unique_ptr<nfui::Page> source, forge, macros, mod, output;
    juce::TooltipWindow tooltip { this, 700 };
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuroforgeEditor)
};
