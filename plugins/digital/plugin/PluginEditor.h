#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 150x130, and either
 *  scales with its bounds. */
class DigitalKnob : public juce::Slider
{
public:
    DigitalKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
           std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_;
};

class DigitalAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DigitalAudioProcessorEditor(DigitalAudioProcessor&);
    ~DigitalAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the line at a glow level (0 to 1) with a pulse at the given position (0 lower left to 1 upper right,
     *  negative for none). Used by the snapshot tool to render a frame. */
    void showGlow(float energy, float pulse);

private:
    void timerCallback() override;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DigitalAudioProcessor& processor_;
    juce::Image background_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    float pulse_ = -1.0f;        // a note start travels along the line, lower left to upper right, about 250 ms
    unsigned lastNotes_ = 0;
    bool previewHold_ = false;

    DigitalKnob feedback_, ratio_, detune_, crunch_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> feedbackA_, ratioA_, detuneA_, crunchA_, attackA_, decayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalAudioProcessorEditor)
};
