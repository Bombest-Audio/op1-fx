#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "pulse_core.h"
}

class PulseAudioProcessor : public juce::AudioProcessor
{
public:
    PulseAudioProcessor();
    ~PulseAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Pulse"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; } // longest release

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Output level (0 to 1) since the last call, then cleared. The editor reads it on its timer. */
    float takeLevel() noexcept { return level_.exchange(0.0f); }
    int activeVoices() const noexcept { return voices_.load(std::memory_order_relaxed); }

private:
    void handleMidi(const juce::MidiMessage& m);
    pulse_params readParams() const;

    pulse_t core_;
    std::vector<float> left_, right_;
    std::atomic<float> level_{0.0f};
    std::atomic<int> voices_{0};

    std::atomic<float>* shape_ = nullptr;
    std::atomic<float>* sweep_ = nullptr;
    std::atomic<float>* rate_ = nullptr;
    std::atomic<float>* detune_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_param_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PulseAudioProcessor)
};
