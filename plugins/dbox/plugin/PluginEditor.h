#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate in design/BRIEF.md. Drag vertically, double-click for the default.
 *  A big knob lives in a 240x250 design space, a small one in 150x130, and either scales with its bounds. */
class DBoxKnob : public juce::Slider
{
public:
    DBoxKnob(juce::String label, juce::Colour tone, bool small, std::function<juce::String(double)> format,
             std::function<juce::String(double)> caption = {});
    void paint(juce::Graphics&) override;

private:
    juce::String label_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_, caption_;
};

class DBoxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DBoxAudioProcessorEditor(DBoxAudioProcessor&);
    ~DBoxAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Light the pads in a 24-bit key mask at full brightness and hold them. Used by the snapshot tool. */
    void showPads(unsigned mask);

private:
    void timerCallback() override;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DBoxAudioProcessor& processor_;
    juce::Image background_;
    float lit_[DBOX_KEYS] = {};   // pad brightness, 1 at a hit, falls over about 250 ms
    float energy_ = 0.0f;         // output level, falls over about 200 ms
    bool previewHold_ = false;

    DBoxKnob pitch_, wave_, decay_, mode_, mix2_, wave2_, decay2_, filter_, atk_, dec_, sus_, rel_, level_, bend_;
    std::unique_ptr<Attachment> a_[14];
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DBoxAudioProcessorEditor)
};
