#pragma once

#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** Big rotary control for the four engine knobs. Laid out in a 240x250 design space and scaled with its bounds.
 *  Drag vertically, double-click to return to the default. */
class ClusterKnob : public juce::Slider
{
public:
    ClusterKnob(juce::String label, juce::String caption, juce::Colour tone,
                std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

/** Small horizontal control for the envelope, level and bend range. Designed at 96x64 and scaled with its bounds. */
class ClusterMini : public juce::Slider
{
public:
    ClusterMini(juce::String label, juce::Colour tone, std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class ClusterAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClusterAudioProcessorEditor(ClusterAudioProcessor&);
    ~ClusterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Freeze the oscillator dots and glow at fixed values. Used by the snapshot tool to render a frame. */
    void showActivity(const std::array<float, CLUSTER_OSCS>& act, float level);

private:
    void timerCallback() override;
    static juce::Rectangle<float> dotRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    ClusterAudioProcessor& processor_;
    juce::Image background_;
    std::array<float, CLUSTER_OSCS> dots_{};   // smoothed per-oscillator brightness, 0 to 1
    float glow_ = 0.0f;                        // smoothed output level, 0 to 1
    bool previewHold_ = false;

    ClusterKnob oscs_, feedback_, spread_, wander_;
    ClusterMini attack_, decay_, sustain_, release_, level_, bend_;
    std::unique_ptr<Attachment> oscsA_, feedbackA_, spreadA_, wanderA_, attackA_, decayA_, sustainA_, releaseA_, levelA_, bendA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClusterAudioProcessorEditor)
};
