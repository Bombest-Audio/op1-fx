#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "op1fx_presets.h"
#include <memory>
#include <vector>

extern "C" {
#include "phone_core.h"
}

class PhoneAudioProcessor : public juce::AudioProcessor
{
public:
    PhoneAudioProcessor();
    ~PhoneAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Phone"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.3; }

    int getNumPrograms() override { return presets_.count(); }
    int getCurrentProgram() override { return presets_.current(); }
    void setCurrentProgram(int index) override { presets_.select(apvts, index); }
    const juce::String getProgramName(int index) override { return presets_.name(index); }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    op1fx::Presets presets_{OP1FX_PRESET_ARGS};
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Level of the loop gate (0 to 1) since the last call, then cleared. Called from the editor's timer;
     *  the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }
    /** Number of fragment changes since the last call, then cleared. Same rules. */
    int takeSwaps() noexcept { return swaps_.exchange(0); }

private:
    phone_params readParams() const;

    std::unique_ptr<phone_t> core_; // about 1.2 MB, so on the heap
    std::vector<float> scratch_;
    std::atomic<float> activity_{0.0f};
    std::atomic<int> swaps_{0};

    std::atomic<float>* tone_ = nullptr;
    std::atomic<float>* phonic_ = nullptr;
    std::atomic<float>* baud_ = nullptr;
    std::atomic<float>* telematic_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhoneAudioProcessor)
};
