#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "op1fx_presets.h"
#include <memory>
#include <vector>

extern "C" {
#include "cwo_core.h"
}

class CwoAudioProcessor : public juce::AudioProcessor
{
public:
    CwoAudioProcessor();
    ~CwoAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cwo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return CWO_MAX_DELAY_MS * 0.001 * 8.0; }

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

    /** Loudest delayed, shifted sample since the last call (0 to about 1.5), then cleared. Called from the
     *  editor's timer; the audio thread only ever raises it. */
    float takeActivity() noexcept { return activity_.exchange(0.0f); }

private:
    // The core holds about 1 MB of delay memory, so it lives on the heap.
    std::unique_ptr<cwo_t> core_;
    std::vector<float> scratch_;
    std::atomic<float> activity_{0.0f};

    std::atomic<float>* freq_ = nullptr;
    std::atomic<float>* delay_ = nullptr;
    std::atomic<float>* fdback_ = nullptr;
    std::atomic<float>* sideband_ = nullptr;
    std::atomic<float>* mix_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CwoAudioProcessor)
};
