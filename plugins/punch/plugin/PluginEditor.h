#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/Punch.dc.html.
 *  Drag vertically, double-click to return to the default. The component is laid
 *  out in a 240x250 design space and scales with its bounds. */
class PunchKnob : public juce::Slider
{
public:
    PunchKnob(juce::String label, juce::String caption, juce::Colour tone,
              std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class PunchAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PunchAudioProcessorEditor(PunchAudioProcessor&);
    ~PunchAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Jump the boxer to a punch strength (0 to 1) and hold it. Used by the snapshot tool to render a frame. */
    void showPunch(float strength);

private:
    void timerCallback() override;
    static juce::Rectangle<float> boxerRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PunchAudioProcessor& processor_;
    juce::Image background_, boxer_;
    float lunge_ = 0.0f;      // 0 at rest, 1 at the hardest hit; rises instantly, falls over about 150 ms
    float swing_ = 0.0f;      // slower follower of lunge_, drives the follow-through
    double phase_ = 0.0;      // idle breathing, radians
    bool previewHold_ = false;
    PunchKnob freq_, punch_, rounds_, power_;
    std::unique_ptr<Attachment> freqA_, punchA_, roundsA_, powerA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchAudioProcessorEditor)
};
