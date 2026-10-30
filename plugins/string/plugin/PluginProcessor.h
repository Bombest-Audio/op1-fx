#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

extern "C" {
#include "string_core.h"

#include <cmath>

/** Envelope times are stored as a linear 0..1 parameter and mapped to seconds on an exponential scale. A skewed
 *  parameter range does not survive a state save and reload bit for bit, and CLAP hosts compare the values. */
namespace stringtime
{
constexpr float kAttackLo = 0.001f, kAttackHi = 4.0f;
constexpr float kLongLo = 0.005f, kLongHi = 8.0f; /* decay and release */
inline float toSeconds(float x, float lo, float hi) { return lo * std::pow(hi / lo, x); }
inline float toNorm(float s, float lo, float hi) { return std::log(s / lo) / std::log(hi / lo); }
} // namespace stringtime
}

class StringAudioProcessor : public juce::AudioProcessor
{
public:
    StringAudioProcessor();
    ~StringAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "String"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 24.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Output level and per-voice activity since the last call, then cleared. The audio thread only ever raises them.
     *  Called from the editor's timer. voiceNote is -1 for a voice that is not sounding. */
    struct Activity
    {
        float level = 0.0f;
        std::array<float, OPST_VOICES> voice{};
        std::array<int, OPST_VOICES> voiceNote{};
    };
    Activity takeActivity() noexcept;

private:
    void applyParams();
    void handleMidi(const juce::MidiMessage& m);

    std::unique_ptr<opst_t> core_; // about 0.5 MB, so on the heap
    std::vector<float> scratch_;
    std::atomic<float> level_{0.0f};
    std::array<std::atomic<float>, OPST_VOICES> voiceLevel_;
    std::array<std::atomic<int>, OPST_VOICES> voiceNote_;
    std::atomic<float>* bendRange_ = nullptr;
    std::array<std::atomic<float>*, 10> p_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StringAudioProcessor)
};
