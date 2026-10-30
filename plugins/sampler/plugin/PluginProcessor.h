#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>

extern "C" {
#include "sampler_core.h"
}

/** An immutable mono sample. Built on the message thread, read on the audio thread. */
struct SamplerData
{
    std::vector<float> data;
    float rate = 44100.0f;
};

class SamplerAudioProcessor : public juce::AudioProcessor
{
public:
    SamplerAudioProcessor();
    ~SamplerAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sampler"; }
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Load a WAV or AIFF file as the sample. Message thread. Stereo is mixed to mono, anything over 30 s is cut.
     *  Returns false and keeps the current sample if the file cannot be read. With setRoot the Root Note
     *  parameter follows the file's own root note when it has one (a WAV smpl chunk), else 60. */
    bool loadSampleFile(const juce::File& file, bool setRoot = true);
    /** Switch to one of the generated samples (0 Bar, 1 Pad, 2 Pluck). Message thread. */
    void loadDemoSample(int kind, bool setRoot = true);

    /** What the editor shows about the loaded sample. Message thread. */
    struct SampleInfo
    {
        juce::String name;
        juce::String path;         // empty for a generated sample
        bool missing = false;      // the saved path could not be read, a generated sample is playing instead
        std::array<float, 128> peaks{};
    };
    SampleInfo getSampleInfo() const;

    /** What the editor animates from. Called from the editor's 60 Hz timer; the audio thread only raises the peak. */
    struct Activity
    {
        float peak = 0.0f;       // largest output sample since the last call, 0 to 1
        int voices = 0;          // voices sounding at the end of the last block
        unsigned notes = 0;      // changes whenever a note starts
        float head = -1.0f;      // playhead of the newest voice, 0 to 1 of the sample, negative when silent
    };
    Activity takeActivity() noexcept;

private:
    sampler_params readParams() const;
    void publish(std::unique_ptr<SamplerData> data, const SampleInfo& info);
    void restoreSampleFromState();

    sampler_t core_;
    std::vector<float> scratch_;
    float lastRate_ = 44100.0f;

    // Sample hand-over. The message thread makes a SamplerData and stores it in latest_; the audio thread notices at the
    // start of a block, gives it to the core and records it in inUse_. Old data is freed only when it is neither of
    // those and at least three loads old, so a block in progress never loses what it reads.
    std::atomic<const SamplerData*> latest_{nullptr};
    std::atomic<const SamplerData*> inUse_{nullptr};
    const SamplerData* current_ = nullptr; // audio thread only
    juce::CriticalSection ownLock_;
    std::vector<std::unique_ptr<SamplerData>> owned_;
    SampleInfo info_;

    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<unsigned> notes_{0};
    std::atomic<float> head_{-1.0f};

    std::atomic<float>* start_ = nullptr;
    std::atomic<float>* end_ = nullptr;
    std::atomic<float>* tune_ = nullptr;
    std::atomic<float>* loop_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_ = nullptr;
    std::atomic<float>* root_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerAudioProcessor)
};
