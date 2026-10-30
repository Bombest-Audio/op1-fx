#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "delay_core.h"
}

class DelayAudioProcessor : public juce::AudioProcessor
{
public:
    DelayAudioProcessor();
    ~DelayAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Strongest echo level (0 to about 1) since the last call, then cleared.
     *  Called from the editor's timer; the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }

private:
    delay_params currentParams() const;
    void allocate(double sampleRate);

    delay_t core_;
    std::vector<float> memL_, memR_; // delay line memory, owned here because the core never allocates
    std::vector<float> scratch_;
    std::atomic<float> activity_{0.0f};

    std::atomic<float>* time_ = nullptr;
    std::atomic<float>* speed_ = nullptr;
    std::atomic<float>* fdb_ = nullptr;
    std::atomic<float>* input_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DelayAudioProcessor)
};
