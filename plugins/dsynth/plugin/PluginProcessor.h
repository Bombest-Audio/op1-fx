#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "dsynth_core.h"
}

class DSynthAudioProcessor : public juce::AudioProcessor
{
public:
    DSynthAudioProcessor();
    ~DSynthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
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

    /** What the editor animates from. Called from the editor's 60 Hz timer; the audio thread only raises the peak. */
    struct Activity
    {
        float peak = 0.0f;       // largest output sample since the last call, 0 to 1
        int voices = 0;          // voices sounding at the end of the last block
        unsigned notes = 0;      // changes whenever a note starts
    };
    Activity takeActivity() noexcept;

private:
    dsynth_params readParams() const;

    dsynth_t core_;
    std::vector<float> scratch_;
    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<unsigned> notes_{0};

    std::atomic<float>* mod_ = nullptr;
    std::atomic<float>* wave_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* filter_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* ampDecay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DSynthAudioProcessor)
};

/** Readout text for the DECAY knob: a decay time up to the middle, then "ATK" and the attack time. */
juce::String DSynthAudioProcessor_decayText(float knob);
