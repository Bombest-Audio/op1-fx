#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

extern "C" {
#include "drumsampler_core.h"
#include "drumsampler_kit.h"
}

/** One slot's audio. Immutable once built, so the audio thread can read it while the message thread holds a reference. */
struct DrumSample
{
    std::vector<float> left, right;   // right is empty for a mono sample
    double rate = 44100.0;
    juce::String name;                // what the pad shows: the file name without extension, or the default sound's name
    juce::String path;                // empty for a default sound
    juce::String missingPath;         // set when a saved file could not be found and the default sound stands in
    int frames() const noexcept { return static_cast<int>(left.size()); }
};

class DrumSamplerAudioProcessor : public juce::AudioProcessor
{
public:
    DrumSamplerAudioProcessor();
    ~DrumSamplerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DrumSampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return release_ != nullptr ? static_cast<double>(release_->load()) : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // pads in the editor play through this

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String slotParamId(int slot, const char* what);   // slot 0..23 -> "s01_pitch"

    // ---- samples (message thread) ----
    /** Load an audio file into a slot. Returns an empty string on success, else the reason. */
    juce::String loadFile(int slot, const juce::File& file);
    /** Put the generated default sound back in a slot. */
    void resetSlot(int slot);
    std::shared_ptr<const DrumSample> sampleFor(int slot) const;
    unsigned sampleRevision() const noexcept { return revision_.load(); }
    juce::String fileWildcard() const { return formats_.getWildcardForAllFormats(); }
    int selectedSlot() const noexcept { return selected_.load(); }
    void setSelectedSlot(int slot);

    /** What the editor animates from. Called from the editor's 60 Hz timer; the audio thread only raises the peak. */
    struct Activity
    {
        float peak = 0.0f;           // largest output sample since the last call, 0 to 1
        int voices = 0;              // voices sounding at the end of the last block
        unsigned notes = 0;          // changes whenever a note starts
        unsigned hits = 0;           // bit n: slot n started a note since the last call
        unsigned playing = 0;        // bit n: slot n is sounding
        int lastSlot = -1;           // slot of the most recent note on
        float head[DS_SLOTS] = {};   // play position per slot, 0 to 1, negative when silent
    };
    Activity takeActivity() noexcept;

private:
    void applyPendingSamples() noexcept;
    void putSample(int slot, std::shared_ptr<DrumSample> s);
    std::shared_ptr<DrumSample> makeDefault(int slot) const;
    void recordPath(int slot, const juce::String& path);
    void loadSamplesFromState();
    void collectGarbage();

    ds_t core_;
    std::vector<float> scratch_;

    // Slot audio. slots_ and library_ belong to the message thread. The audio thread sees raw pointers through
    // pending_ and records what it has handed to the core in applied_. A sample is only freed when neither
    // pending_ nor applied_ names it, and applied_ is written after the core has stopped reading the old audio.
    std::array<std::shared_ptr<DrumSample>, DS_SLOTS> slots_;
    std::vector<std::shared_ptr<DrumSample>> library_;
    std::array<std::atomic<const DrumSample*>, DS_SLOTS> pending_;
    std::array<std::atomic<const DrumSample*>, DS_SLOTS> applied_;
    std::atomic<unsigned> revision_{0};
    std::atomic<int> selected_{0};
    juce::AudioFormatManager formats_;

    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<unsigned> notes_{0};
    std::atomic<unsigned> hits_{0};
    std::atomic<unsigned> playing_{0};
    std::atomic<int> lastSlot_{-1};
    std::array<std::atomic<float>, DS_SLOTS> head_;

    struct SlotParams
    {
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* in = nullptr;
        std::atomic<float>* out = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* reverse = nullptr;
        std::atomic<float>* gain = nullptr;
    };
    std::array<SlotParams, DS_SLOTS> slotParams_;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumSamplerAudioProcessor)
};
