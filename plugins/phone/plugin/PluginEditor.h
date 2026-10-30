#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md (Claude Design project "Phone faceplate").
 *  Drag vertically, double-click to return to the default. The component is laid out in a 240x250 design
 *  space and scales with its bounds. */
class PhoneKnob : public juce::Slider
{
public:
    PhoneKnob(juce::String label, juce::String caption, juce::Colour tone,
              std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    std::function<juce::String(double)> format_;
};

class PhoneAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PhoneAudioProcessorEditor(PhoneAudioProcessor&);
    ~PhoneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Hold the handset in a lit state (levels 0 to 1) so the snapshot tool can render a frame. */
    void showLevels(float mic, float chipFlash);

private:
    void timerCallback() override;
    static juce::Rectangle<float> artRegion();

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PhoneAudioProcessor& processor_;
    juce::Image background_;
    float mic_ = 0.0f;     // input level seen by the mic capsule; rises instantly, falls over about 100 ms
    float speaker_ = 0.0f; // follows mic_ more slowly and scales with TELEMATIC, like a voice coming out the other end
    float chip_ = 0.0f;    // flashes on each fragment change
    double phase_ = 0.0;   // drives the travelling arcs, radians
    bool previewHold_ = false;
    PhoneKnob tone_, phonic_, baud_, telematic_;
    std::unique_ptr<Attachment> toneA_, phonicA_, baudA_, telematicA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhoneAudioProcessorEditor)
};
