#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

/** A rotary control drawn to match the faceplate design in design/BRIEF.md. Drag vertically, double-click to
 *  return to the default. A big knob is laid out in a 240x250 design space, a compact one in 98x98, and either
 *  scales with its bounds. */
class DsKnob : public juce::Slider
{
public:
    enum class Size { Big, Compact };
    DsKnob(juce::String label, juce::String caption, juce::Colour tone, Size size, std::function<juce::String(double)> format);
    void paint(juce::Graphics&) override;

private:
    juce::String label_, caption_;
    juce::Colour tone_;
    Size size_;
    std::function<juce::String(double)> format_;
};

/** The REV pill. */
class DsToggle : public juce::Button
{
public:
    explicit DsToggle(juce::String text);
    void paintButton(juce::Graphics&, bool over, bool down) override;

private:
    juce::String text_;
};

/** The sample view: waveform, played region, play position, sample name and the LOAD and RESET buttons. */
class DsWaveform : public juce::Component
{
public:
    void setSample(std::shared_ptr<const DrumSample> s, int slot);
    void setRegion(float in, float out) { if (in != in_ || out != out_) { in_ = in; out_ = out; repaint(); } }
    void setPlayhead(float p) { if (p != head_) { head_ = p; repaint(); } }
    void setNotice(const juce::String& text) { notice_ = text; repaint(); }

    std::function<void(bool isIn, float value)> onMarker;   // dragging a marker
    std::function<void()> onLoad, onReset;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { drag_ = 0; }
    void mouseDoubleClick(const juce::MouseEvent&) override;

    static constexpr float kW = 590.0f, kH = 150.0f;

private:
    juce::Rectangle<float> loadBox() const { return {kW - 150.0f, 8.0f, 68.0f, 22.0f}; }
    juce::Rectangle<float> resetBox() const { return {kW - 76.0f, 8.0f, 68.0f, 22.0f}; }
    juce::Point<float> toDesign(juce::Point<float> p) const;

    std::shared_ptr<const DrumSample> sample_;
    int slot_ = 0;
    std::vector<float> lo_, hi_;
    float in_ = 0.0f, out_ = 1.0f, head_ = -1.0f;
    int drag_ = 0;   // 1 in marker, 2 out marker
    juce::String notice_;
};

/** Twenty-four pads in six columns by four rows. Click selects the slot and plays it. */
class DsPadGrid : public juce::Component
{
public:
    explicit DsPadGrid(juce::MidiKeyboardState& keys) : keys_(keys) {}

    void setNames(const std::array<juce::String, DS_SLOTS>& names, const std::array<int, DS_SLOTS>& kinds)
    {
        names_ = names;
        kinds_ = kinds;
        repaint();
    }
    void setSelected(int s) { if (s != selected_) { selected_ = s; repaint(); } }
    void setFlash(int slot, float v) { flash_[static_cast<size_t>(slot)] = v; }
    void tickFlash() { for (auto& f : flash_) f = f > 0.01f ? f * 0.9f : 0.0f; }   // about 250 ms to fade
    void setPlaying(unsigned mask) { if (mask != playing_) { playing_ = mask; repaint(); } }
    void setDropTarget(int slot) { if (slot != drop_) { drop_ = slot; repaint(); } }
    int slotAt(juce::Point<int> local) const;   // -1 when between pads

    std::function<void(int)> onSelect;
    std::function<void(int)> onLoad, onReset;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr float kW = 590.0f, kH = 248.0f;

private:
    juce::Rectangle<float> padRect(int slot) const;

    juce::MidiKeyboardState& keys_;
    std::array<juce::String, DS_SLOTS> names_;
    std::array<int, DS_SLOTS> kinds_{};   // 0 default sound, 1 user file, 2 saved file that is missing
    std::array<float, DS_SLOTS> flash_{};
    int selected_ = 0, drop_ = -1, held_ = -1;
    unsigned playing_ = 0;
};

class DrumSamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::FileDragAndDropTarget,
                                        private juce::Timer
{
public:
    explicit DrumSamplerAudioProcessorEditor(DrumSamplerAudioProcessor&);
    ~DrumSamplerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override { pads_.setDropTarget(-1); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /** Hold the art at a glow level (0 to 1), flash one pad and park the play position, for the snapshot tool.
     *  flashSlot negative for none, playhead negative for none. */
    void showPreview(float energy, int flashSlot, float playhead);

private:
    void timerCallback() override;
    void bindSlot(int slot);
    void selectSlot(int slot);
    void refreshSamples();
    void chooseFile(int slot);
    void loadInto(int slot, const juce::File& file);
    juce::Rectangle<int> scaled(float x, float y, float w, float h) const;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    DrumSamplerAudioProcessor& processor_;
    juce::Image background_;

    float energy_ = 0.0f;        // output level, rises at once and falls over about 200 ms
    unsigned lastNotes_ = 0;
    bool previewHold_ = false;
    int boundSlot_ = -1;
    unsigned lastRevision_ = ~0u;
    int noticeFrames_ = 0;

    DsKnob pitch_, in_, out_, mode_, attack_, decay_, sustain_, release_, level_, gain_;
    DsToggle reverse_{"REV"};
    DsWaveform wave_;
    DsPadGrid pads_;

    std::unique_ptr<SliderAttachment> pitchA_, inA_, outA_, modeA_, gainA_;
    std::unique_ptr<ButtonAttachment> reverseA_;
    std::unique_ptr<SliderAttachment> attackA_, decayA_, sustainA_, releaseA_, levelA_;

    std::unique_ptr<juce::FileChooser> chooser_;
    juce::ComponentBoundsConstrainer constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerAudioProcessorEditor)
};
