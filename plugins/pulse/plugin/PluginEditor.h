#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to return to the
 *  default. Big knobs are laid out in a 240x250 design space, small ones in 128x120, and both scale with their bounds. */
class PulseKnob : public juce::Slider
{
public:
    PulseKnob(juce::String label, juce::String caption, juce::Colour tone, bool small, bool bipolar,
              std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_, bipolar_;
    std::function<juce::String(double)> format_;
};

class PulseAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PulseAudioProcessorEditor(PulseAudioProcessor&);
    ~PulseAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the neon glow at a level (0 to 1) with a number of lit voice dots. Used by the snapshot tool to render a frame. */
    void showActivity(float level, int voices);

private:
    void timerCallback() override;
    juce::Rectangle<int> toPixels(juce::Rectangle<float> designRect) const;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PulseAudioProcessor& processor_;
    juce::Image background_;
    float glow_ = 0.0f;   // smoothed output level, rises instantly and falls over about 250 ms
    int voices_ = 0;
    bool previewHold_ = false;
    PulseKnob shape_, sweep_, rate_, detune_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> shapeA_, sweepA_, rateA_, detuneA_, attackA_, decayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulseAudioProcessorEditor)
};
