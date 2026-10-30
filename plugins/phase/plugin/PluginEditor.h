#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to return
 *  to the default. Big knobs are laid out in a 240x250 design space and mini knobs in 100x120; both scale with the
 *  component's bounds. */
class PhaseKnob : public juce::Slider
{
public:
    PhaseKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String(double)> format,
              bool mini);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
    bool mini_;
};

class PhaseAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhaseAudioProcessorEditor(PhaseAudioProcessor&);
    ~PhaseAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Freeze the scope in a playing state (level 0 to 1, sounding voices 0 to 8). Used by the snapshot tool. */
    void showDemo(float level, int voices);

private:
    void timerCallback() override;
    void paintScope(juce::Graphics&);
    static juce::Rectangle<float> scopeRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PhaseAudioProcessor& processor_;
    juce::Image background_;
    float glow_ = 0.0f;      // smoothed output level, 0 to 1
    int voices_ = 0;
    double sweep_ = 0.0;     // the marker's position along the cycle, 0 to 1
    bool demoHold_ = false;
    PhaseKnob a_, b_, r_, w_, attack_, decay_, sustain_, release_, level_, bend_;
    std::unique_ptr<Attachment> aA_, bA_, rA_, wA_, attackA_, decayA_, sustainA_, releaseA_, levelA_, bendA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseAudioProcessorEditor)
};
