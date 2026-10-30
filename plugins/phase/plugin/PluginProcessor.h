#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "phase_core.h"
}

class PhaseAudioProcessor : public juce::AudioProcessor
{
public:
    PhaseAudioProcessor();
    ~PhaseAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Phase"; }
    bool acceptsMidi() const override { return true; }
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

    /** Loudest output sample since the last call (0 to 1), then cleared. Called from the editor's timer; the audio
     *  thread only ever raises it. */
    float takeLevel() noexcept { return level_.exchange(0.0f); }
    /** Number of sounding voices after the last block. */
    int voiceCount() const noexcept { return voices_.load(std::memory_order_relaxed); }

private:
    void readParams(phase_params& p) const;
    void handleMidi(const juce::MidiMessage& m);
    void renderRange(float* l, float* r, int start, int count);

    phase_t core_;
    std::vector<float> scratch_;
    std::atomic<float> level_{0.0f};
    std::atomic<int> voices_{0};

    std::atomic<float>* a_ = nullptr;
    std::atomic<float>* b_ = nullptr;
    std::atomic<float>* r_ = nullptr;
    std::atomic<float>* w_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_param_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseAudioProcessor)
};
