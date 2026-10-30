#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

/** A rotary control drawn to match the faceplate in design/BRIEF.md. Drag vertically, double-click for the default.
 *  Big knobs lay out in a 240x250 design space, mini knobs in 80x100. Either scales with its bounds. A bipolar knob
 *  fills its ring from the centre instead of from the left end. */
class DnaKnob : public juce::Slider
{
public:
    DnaKnob(juce::String label, juce::String caption, juce::Colour tone, bool mini, bool bipolar,
            std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    void paintBig(juce::Graphics&);
    void paintMini(juce::Graphics&);

    juce::String label_, caption_;
    juce::Colour tone_;
    bool mini_, bipolar_;
    std::function<juce::String(double)> format_;
};

class DnaAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DnaAudioProcessorEditor(DnaAudioProcessor&);
    ~DnaAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the helix at a glow level (0 to 1) and a voice count. Used by the snapshot tool to render a frame. */
    void showGlow(float level, int voices);

private:
    void timerCallback() override;
    static juce::Rectangle<float> helixRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DnaAudioProcessor& processor_;
    juce::Image background_;
    float glow_ = 0.0f;       // follows the output peak: rises at once, falls over about 250 ms
    float pulse_ = 0.0f;      // set to 1 on each new note, decays over about 300 ms
    int voices_ = 0;
    unsigned lastNotes_ = 0;
    double phase_ = 0.0;      // slow shimmer, radians
    bool hold_ = false;

    DnaKnob mutate_, bits_, sub_, grit_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> mutateA_, bitsA_, subA_, gritA_, attackA_, decayA_, sustainA_, releaseA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DnaAudioProcessorEditor)
};
