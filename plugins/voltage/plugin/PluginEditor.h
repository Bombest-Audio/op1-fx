#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match design/BRIEF.md. Drag vertically, double-click for the default.
 *  Laid out in a 240x250 design space (or a small 110x120 one) and scaled with its bounds. */
class VoltageKnob : public juce::Slider
{
public:
    VoltageKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String(double)> format,
                bool small = false);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
    bool small_;
};

class VoltageAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoltageAudioProcessorEditor(VoltageAudioProcessor&);
    ~VoltageAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the spark at a level (0 to 1). Used by the snapshot tool to render a frame. */
    void showLevel(float level);

private:
    void timerCallback() override;
    static juce::Rectangle<float> sparkRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    VoltageAudioProcessor& processor_;
    juce::Image background_;
    float glow_ = 0.0f;
    double phase_ = 0.0;
    int seed_ = 1;
    bool hold_ = false;
    VoltageKnob xor_, fb_, filter_, detune_, attack_, decay_, sustain_, release_, level_;
    std::unique_ptr<Attachment> a_[9];
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoltageAudioProcessorEditor)
};
