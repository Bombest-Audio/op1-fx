#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "punch_core.h"
}

class PunchAudioProcessor : public juce::AudioProcessor
{
public:
    PunchAudioProcessor();
    ~PunchAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Punch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Strongest transient seen since the last call (0 to 3, about 1 for a clear hit), then cleared.
     *  Called from the editor's timer; the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }

private:
    punch_t core_;
    std::vector<float> scratch_;
    std::atomic<float> activity_{0.0f};

    std::atomic<float>* freq_ = nullptr;
    std::atomic<float>* punch_ = nullptr;
    std::atomic<float>* rounds_ = nullptr;
    std::atomic<float>* power_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PunchAudioProcessor)
};
