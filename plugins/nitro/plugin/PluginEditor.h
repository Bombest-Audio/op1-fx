#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md.
 *  Drag vertically, double-click to return to the default. The component is laid
 *  out in a 240x250 design space and scales with its bounds. */
class NitroKnob : public juce::Slider
{
public:
    NitroKnob(juce::String label, juce::String caption, juce::Colour tone, float readoutSize,
              std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    float readoutSize_;
    std::function<juce::String(double)> format_;
};

class NitroAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NitroAudioProcessorEditor(NitroAudioProcessor&);
    ~NitroAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the glow at a fixed level (0 to 1) and stop following the signal. Used by the snapshot tool. */
    void showLevel(float level);

private:
    void timerCallback() override;
    static juce::Rectangle<float> artRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    NitroAudioProcessor& processor_;
    juce::Image background_;
    float level_ = 0.0f;   // follows the input level: rises quickly, falls over about 300 ms
    double phase_ = 0.0;   // slow idle drift, radians
    bool hold_ = false;
    NitroKnob freq_, res_, shape_, drive_;
    std::unique_ptr<Attachment> freqA_, resA_, shapeA_, driveA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NitroAudioProcessorEditor)
};
