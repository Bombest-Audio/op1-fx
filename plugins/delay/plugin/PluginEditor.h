#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

/** A rotary control drawn to match the faceplate design (Claude Design project "Delay faceplate").
 *  Drag vertically, double-click to return to the default. The component is laid out in a 240x250
 *  design space and scales with its bounds. The readout can depend on other parameters (the TIME
 *  readout is the real delay, which also depends on SPEED), so refreshReadout() repaints when it changes. */
class DelayKnob : public juce::Slider
{
public:
    DelayKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String()> format);
    void paint(juce::Graphics&) override;
    void refreshReadout();

private:
    juce::String label_, caption_, shown_;
    juce::Colour tone_;
    std::function<juce::String()> format_;
};

class DelayAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DelayAudioProcessorEditor(DelayAudioProcessor&);
    ~DelayAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Freeze three echo rings at different ages for a given echo level (0 to 1). Used by the snapshot tool. */
    void showEcho(float level);

private:
    struct Ring
    {
        float age = 1.0f;   // 0 when born, 1 when gone
        float level = 0.0f; // echo level at birth, sets the brightness
    };

    void timerCallback() override;
    static juce::Rectangle<float> rippleRegion();
    float valueOf(const char* id) const;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DelayAudioProcessor& processor_;
    juce::Image background_;
    std::array<Ring, 6> rings_;
    float level_ = 0.0f;      // fast follower of the echo level, drives the glow
    float slowLevel_ = 0.0f;  // slow follower, so a steady echo does not spawn a ring every frame
    int sinceSpawn_ = 1000;   // timer ticks since the last ring
    bool previewHold_ = false;
    DelayKnob time_, speed_, fdb_, input_;
    std::unique_ptr<Attachment> timeA_, speedA_, fdbA_, inputA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayAudioProcessorEditor)
};
