#pragma once

#include <cmath>
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

extern "C" {
#include "voltage_core.h"
}

/** Envelope times are stored as a linear 0..1 control and mapped to seconds on an exponential scale, so the saved
 *  value round-trips exactly. The MPC port uses the same mapping. */
namespace voltagetime
{
constexpr float kAttackLo = 0.0005f, kAttackHi = 5.0f;
constexpr float kLongLo = 0.005f, kLongHi = 10.0f;
inline float toSeconds(float x, float lo, float hi) { return lo * std::pow(hi / lo, x); }
inline float toNorm(float s, float lo, float hi) { return std::log(s / lo) / std::log(hi / lo); }
} // namespace voltagetime

class VoltageAudioProcessor : public juce::AudioProcessor
{
public:
    VoltageAudioProcessor();
    ~VoltageAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Voltage"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Output peak since the last call (0 to 1), then cleared. Called from the editor's timer. */
    float takeLevel() noexcept { return level_.exchange(0.0f); }
    /** Number of sounding voices at the last block. */
    int activeVoices() const noexcept { return voices_.load(); }

private:
    opvl_params readParams() const;
    void handleMidi(const juce::MidiMessage&);

    opvl_t core_;
    std::atomic<float> level_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<float>* xor_ = nullptr;
    std::atomic<float>* fb_ = nullptr;
    std::atomic<float>* filter_ = nullptr;
    std::atomic<float>* detune_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_p_ = nullptr;
    float bendRange_ = 2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoltageAudioProcessor)
};
