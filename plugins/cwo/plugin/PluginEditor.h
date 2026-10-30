#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in the Claude Design project "Cwo faceplate".
 *  Drag vertically, double-click to return to the default. The component is laid out in a 240x250
 *  design space and scales with its bounds. */
class CwoKnob : public juce::Slider
{
public:
    CwoKnob(juce::String label, juce::String caption, juce::Colour tone,
            std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class CwoAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CwoAudioProcessorEditor(CwoAudioProcessor&);
    ~CwoAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the halo at a signal level (0 to 1) and an angle in turns. Used by the snapshot tool to render a frame. */
    void showLevel(float level, float turns);

private:
    void timerCallback() override;
    static juce::Rectangle<float> haloRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    CwoAudioProcessor& processor_;
    juce::Image background_;
    float level_ = 0.0f;      // smoothed signal level, 0..1: rises quickly, falls over about 300 ms
    double angle_ = 0.0;      // halo rotation in radians, integrated from the FREQ and SIDEBAND controls
    double phase_ = 0.0;      // slow shimmer, radians
    bool previewHold_ = false;
    CwoKnob freq_, delay_, fdback_, sideband_;
    std::unique_ptr<Attachment> freqA_, delayA_, fdbackA_, sidebandA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CwoAudioProcessorEditor)
};
