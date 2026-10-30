#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 150x130, and either
 *  scales with its bounds. */
class DrWaveKnob : public juce::Slider
{
public:
    DrWaveKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
           std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_;
};

class DrWaveAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DrWaveAudioProcessorEditor(DrWaveAudioProcessor&);
    ~DrWaveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the wave trace at an energy level (0 to 1) with a pulse (0 to 1.4, negative for none). Used by the
     *  snapshot tool to render a frame. */
    void showGlow(float energy, float pulse);

private:
    void timerCallback() override;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DrWaveAudioProcessor& processor_;
    juce::Image background_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    float pulse_ = -1.0f;        // a note start sweeps a bright spot along the trace, left to right, about 250 ms
    std::unique_ptr<dw_t> bank_ = std::make_unique<dw_t>(); // a private copy of the wave bank for drawing
    unsigned lastNotes_ = 0;
    bool previewHold_ = false;

    DrWaveKnob wave_, blend_, motion_, fold_, tone_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> waveA_, blendA_, motionA_, foldA_, toneA_, attackA_, decayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrWaveAudioProcessorEditor)
};
