#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 150x130, and either
 *  scales with its bounds. */
class DSynthKnob : public juce::Slider
{
public:
    DSynthKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
               std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_;
};

class DSynthAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DSynthAudioProcessorEditor(DSynthAudioProcessor&);
    ~DSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the pad at a glow level (0 to 1) with a ripple at the given age (0 just struck, 1 faded, negative for
     *  none). Used by the snapshot tool to render a frame. */
    void showHit(float energy, float ripple);

private:
    void timerCallback() override;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DSynthAudioProcessor& processor_;
    juce::Image background_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    float ripple_ = -1.0f;       // 0 at a new note, 1 when the ring has reached the rim, negative when idle
    unsigned lastNotes_ = 0;
    bool previewHold_ = false;

    DSynthKnob mod_, wave_, decay_, filter_, attack_, ampDecay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> modA_, waveA_, decayA_, filterA_, attackA_, ampDecayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSynthAudioProcessorEditor)
};
