#include "PluginEditor.h"

NeuroforgeEditor::NeuroforgeEditor(NeuroforgeProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&laf);
    setLookAndFeel(&laf);

    addAndMakeVisible(content);
    top = std::make_unique<nfui::TopBar>(p);
    source = nfui::makeSourcePanel(p);
    forge = nfui::makeForgePanel(p);
    macros = nfui::makeMacroPanel(p);
    mod = nfui::makeModPanel(p);
    output = nfui::makeOutputPanel(p);
    for (juce::Component* c : { (juce::Component*) top.get(), (juce::Component*) source.get(), (juce::Component*) forge.get(),
                                (juce::Component*) macros.get(), (juce::Component*) mod.get(), (juce::Component*) output.get() })
        content.addAndMakeVisible(c);

    // fixed design layout, scaled as a whole
    top->setBounds(8, 6, 1264, 46);
    source->setBounds(8, 58, 316, 530);
    forge->setBounds(332, 58, 688, 530);
    output->setBounds(1028, 58, 244, 530);
    macros->setBounds(8, 596, 316, 196);
    mod->setBounds(332, 596, 940, 196);

    constrainer.setFixedAspectRatio((double) kW / (double) kH);
    constrainer.setSizeLimits(640, 400, 2560, 1600);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(kW, kH);
    startTimerHz(30);
}

NeuroforgeEditor::~NeuroforgeEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &laf) juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void NeuroforgeEditor::paint(juce::Graphics& g) { g.fillAll(nfui::cBg); }

void NeuroforgeEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    content.setTransform(juce::AffineTransform::scale(s));
    content.setBounds(0, 0, kW, kH);
}

void NeuroforgeEditor::timerCallback() { nfui::Tickable::tickAll(); }
