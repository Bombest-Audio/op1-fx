#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

extern "C" {
#include "dbox_core.h"
}

class DBoxAudioProcessor : public juce::AudioProcessor
{
public:
    DBoxAudioProcessor();
    ~DBoxAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DBox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return release_ != nullptr ? static_cast<double>(release_->load()) + 2.5 : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** What the editor animates from, read on its 60 Hz timer. The audio thread only raises the peak and the key mask. */
    struct Activity
    {
        float peak = 0.0f;     // largest output sample since the last call
        int voices = 0;
        unsigned notes = 0;
        unsigned keys = 0;     // bit k set when drum key k was struck since the last call
    };
    Activity takeActivity() noexcept;

private:
    dbox_params readParams() const;

    dbox_t core_;
    std::vector<float> scratch_;
    std::atomic<float> peak_{0.0f};
    std::atomic<int> voices_{0};
    std::atomic<unsigned> notes_{0};
    std::atomic<unsigned> keys_{0};

    std::atomic<float>* pitch_ = nullptr;
    std::atomic<float>* wave_ = nullptr;
    std::atomic<float>* decay_ = nullptr;
    std::atomic<float>* mode_ = nullptr;
    std::atomic<float>* mix2_ = nullptr;
    std::atomic<float>* wave2_ = nullptr;
    std::atomic<float>* decay2_ = nullptr;
    std::atomic<float>* filter_ = nullptr;
    std::atomic<float>* atk_ = nullptr;
    std::atomic<float>* dec_ = nullptr;
    std::atomic<float>* sus_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    std::atomic<float>* level_ = nullptr;
    std::atomic<float>* bend_ = nullptr;
    std::atomic<float>* root_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DBoxAudioProcessor)
};
