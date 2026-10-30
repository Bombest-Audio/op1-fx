#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "cluster_core.h"
}

class ClusterAudioProcessor : public juce::AudioProcessor
{
public:
    ClusterAudioProcessor();
    ~ClusterAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cluster"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Per-oscillator strength (0 to 1) since the last call. Called from the editor's timer; the audio thread only raises it. */
    void takeActivity(std::array<float, CLUSTER_OSCS>& out) noexcept;
    /** Output peak since the last call, then cleared. */
    float takeLevel() noexcept { return level_.exchange(0.0f); }

private:
    cluster_params readParams() const;
    void applyMidi(const juce::MidiMessage& m);

    cluster_t core_;
    std::vector<float> scratch_;
    std::atomic<float> level_{0.0f};
    std::array<std::atomic<float>, CLUSTER_OSCS> activity_;

    std::atomic<float>* oscs_ = nullptr;
    std::atomic<float>* feedback_ = nullptr;
    std::atomic<float>* spread_ = nullptr;
    std::atomic<float>* wander_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_param_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClusterAudioProcessor)
};
