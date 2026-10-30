#pragma once

#include "PluginProcessor.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/Grid.dc.html.
 *  Drag vertically, double-click to return to the default. The component is laid
 *  out in a 240x250 design space and scales with its bounds. */
class GridKnob : public juce::Slider
{
public:
    GridKnob(juce::String label, juce::String caption, juce::Colour tone, std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class GridAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GridAudioProcessorEditor(GridAudioProcessor&);
    ~GridAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Freeze the lattice animation at a hit of the given strength (0 to 1), with two ripples in flight.
     *  Used by the snapshot tool to render a frame. */
    void showHit(float strength);

private:
    struct Ripple
    {
        float age = 1.0f;       // seconds since the hit; a ripple is live while age < kRippleSeconds
        float strength = 0.0f;  // 0 to 1
    };

    void timerCallback() override;
    void spawnRipple(float strength);
    juce::String passText() const;
    static juce::Rectangle<float> latticeRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    GridAudioProcessor& processor_;
    juce::Image background_;
    float glow_ = 0.0f;       // 0 at rest, 1 at a loud wet hit; rises instantly, falls over about 200 ms
    float sinceSpawn_ = 1.0f; // seconds since the last ripple was started
    std::array<Ripple, 4> ripples_{};
    bool previewHold_ = false;
    juce::String pass_;
    GridKnob x_, y_, zfb_, mix_;
    std::unique_ptr<Attachment> xA_, yA_, zfbA_, mixA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridAudioProcessorEditor)
};
