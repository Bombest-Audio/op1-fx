#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "op1fx_presets.h"

extern "C" {
#include "nitro_core.h"
}

class NitroAudioProcessor : public juce::AudioProcessor
{
public:
    NitroAudioProcessor();
    ~NitroAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nitro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

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

    /** Input level seen since the last call (0 to 1), then cleared. Called from the editor's timer;
     *  the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }

private:
    nitro_params currentParams() const noexcept;

    nitro_t core_;
    std::atomic<float> activity_{0.0f};

    std::atomic<float>* freq_ = nullptr;
    std::atomic<float>* res_ = nullptr;
    std::atomic<float>* shape_ = nullptr;
    std::atomic<float>* drive_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NitroAudioProcessor)
};
