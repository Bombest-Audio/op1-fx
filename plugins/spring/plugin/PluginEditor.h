#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/Spring.dc.html.
 *  Drag vertically, double-click to return to the default. The component is laid
 *  out in a 240x250 design space and scales with its bounds. */
class SpringKnob : public juce::Slider
{
public:
    SpringKnob(juce::String label, juce::String caption, juce::Colour tone,
               std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class SpringAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpringAudioProcessorEditor(SpringAudioProcessor&);
    ~SpringAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Freeze the springs at a given excitation (0 to 1) and pulse position (0 to 1). Used by the snapshot tool. */
    void showEnergy(float energy, float pulsePosition);

private:
    void timerCallback() override;
    static juce::Rectangle<float> springRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    SpringAudioProcessor& processor_;
    juce::Image background_;
    float energy_ = 0.0f;     // 0 to 1, follows the tank output: rises fast, falls over about 0.5 s
    float pulse_ = 0.0f;      // 0 to 1, position of the travelling glow along the springs
    bool previewHold_ = false;
    SpringKnob tone_, turns_, damp_, level_;
    std::unique_ptr<Attachment> toneA_, turnsA_, dampA_, levelA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpringAudioProcessorEditor)
};
