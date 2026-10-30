#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 150x130, and either
 *  scales with its bounds. */
class FmKnob : public juce::Slider
{
public:
    FmKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
           std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_;
};

class FmAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FmAudioProcessorEditor(FmAudioProcessor&);
    ~FmAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the cubes at a glow level (0 to 1) with a pulse at the given position (0 top to 1 bottom, negative for
     *  none). Used by the snapshot tool to render a frame. */
    void showGlow(float energy, float pulse);

private:
    void timerCallback() override;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    FmAudioProcessor& processor_;
    juce::Image background_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    float pulse_ = -1.0f;        // a note start travels down the four cubes, top to bottom, about 250 ms
    unsigned lastNotes_ = 0;
    bool previewHold_ = false;

    FmKnob ratio_, index_, feedback_, layer_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> ratioA_, indexA_, feedbackA_, layerA_, attackA_, decayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FmAudioProcessorEditor)
};
