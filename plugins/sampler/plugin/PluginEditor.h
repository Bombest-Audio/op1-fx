#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

juce::String samplerNoteName(int note);

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a small one in 150x130, and either
 *  scales with its bounds. */
class SamplerKnob : public juce::Slider
{
public:
    SamplerKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
                std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    bool small_;
    std::function<juce::String(double)> format_;
};

class SamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::FileDragAndDropTarget,
                                    private juce::Timer
{
public:
    explicit SamplerAudioProcessorEditor(SamplerAudioProcessor&);
    ~SamplerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver_ = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver_ = false; repaint(); }

    /** Hold the glow at a level (0 to 1) with a playhead at the given position (0 top to 1 bottom, negative for
     *  none). Used by the snapshot tool to render a frame. */
    void showGlow(float energy, float head);

private:
    void timerCallback() override;
    void showSampleMenu();
    void loadFile(const juce::File& f);
    juce::Rectangle<int> sampleBarBounds() const;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    SamplerAudioProcessor& processor_;
    juce::Image background_;
    SamplerAudioProcessor::SampleInfo info_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    float head_ = -1.0f;         // playhead, 0 top to 1 bottom
    bool previewHold_ = false;
    bool dragOver_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;

    SamplerKnob start_, end_, tune_, loop_, attack_, decay_, sustain_, release_, level_, root_;
    std::unique_ptr<Attachment> startA_, endA_, tuneA_, loopA_, attackA_, decayA_, sustainA_, releaseA_, levelA_, rootA_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerAudioProcessorEditor)
};
