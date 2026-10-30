#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "grid_core.h"
}

class GridAudioProcessor : public juce::AudioProcessor
{
public:
    GridAudioProcessor();
    ~GridAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Grid"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    // The mesh rings out for a few seconds at the default Z FDB, longer when it is turned up.
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Loudest wet sample since the last call (0 to 1.5, about 0.3 for a normal hit), then cleared.
     *  Called from the editor's timer; the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }

private:
    grid_params currentParams() const noexcept;
    void startCore(double sampleRate);

    grid_t core_{};
    std::vector<float> mesh_;       // working memory for core_, sized from the sample rate
    std::vector<float> scratch_;    // second channel for mono hosts
    std::atomic<float> activity_{0.0f};

    std::atomic<float>* x_ = nullptr;
    std::atomic<float>* y_ = nullptr;
    std::atomic<float>* zfb_ = nullptr;
    std::atomic<float>* mix_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridAudioProcessor)
};
