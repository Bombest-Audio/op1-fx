#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

extern "C" {
#include "drwave_core.h"
}

class DrWaveAudioProcessor : public juce::AudioProcessor
{
public:
    DrWaveAudioProcessor();
    ~DrWaveAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DrWave"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return release_ != nullptr ? static_cast<double>(release_->load()) : 0.0; }

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
    dw_params readParams() const;

    std::unique_ptr<dw_t> core_ = std::make_unique<dw_t>(); // about 600 KB of wave tables, keep it off the stack
    std::vector<float> scratch_;
    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<unsigned> notes_{0};

    std::atomic<float>* wave_ = nullptr;
    std::atomic<float>* blend_ = nullptr;
    std::atomic<float>* motion_ = nullptr;
    std::atomic<float>* fold_ = nullptr;
    std::atomic<float>* tone_ = nullptr;
    std::atomic<float>* attack_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_ = nullptr;
    std::atomic<float>* bend_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrWaveAudioProcessor)
};
