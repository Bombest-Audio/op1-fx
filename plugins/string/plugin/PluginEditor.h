#pragma once

#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 118x110, and both scale
 *  with their bounds. */
class StringKnob : public juce::Slider
{
public:
    StringKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String(double)> format,
               bool small = false);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
    bool small_;
};

class StringAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StringAudioProcessorEditor(StringAudioProcessor&);
    ~StringAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the strings at a vibration strength (0 to 1) with the voice lights on. Used by the snapshot tool. */
    void showVibration(float strength);

private:
    void timerCallback() override;
    static juce::Rectangle<float> artRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    StringAudioProcessor& processor_;
    juce::Image background_, art_;
    float energy_ = 0.0f;   // follows the output level: rises instantly, falls over about 200 ms
    double phase_ = 0.0;    // vibration phase, radians (a visual rate, not the pitch)
    std::array<float, OPST_VOICES> dots_{};
    std::array<bool, OPST_VOICES> dotOn_{};
    bool previewHold_ = false;

    StringKnob pick_, damp_, stiff_, decay_;
    StringKnob attack_, envDecay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> pickA_, dampA_, stiffA_, decayA_, attackA_, envDecayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StringAudioProcessorEditor)
};
