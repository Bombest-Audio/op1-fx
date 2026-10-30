#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kTime{"time", 1};
const juce::ParameterID kSpeed{"speed", 1};
const juce::ParameterID kFdb{"fdb", 1};
const juce::ParameterID kInput{"input", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DelayAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kTime, "Time", NormalisableRange<float>(0.0f, 1.0f),
                                                      DELAY_DEFAULT_TIME));
    layout.add(std::make_unique<AudioParameterFloat>(kSpeed, "Speed", NormalisableRange<float>(0.0f, 1.0f),
                                                      DELAY_DEFAULT_SPEED));
    layout.add(std::make_unique<AudioParameterFloat>(kFdb, "Fdb", NormalisableRange<float>(0.0f, 1.0f),
                                                      DELAY_DEFAULT_FDB));
    layout.add(std::make_unique<AudioParameterFloat>(kInput, "Input", NormalisableRange<float>(0.0f, 1.0f),
                                                      DELAY_DEFAULT_INPUT));
    return layout;
}

DelayAudioProcessor::DelayAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DelayState", createLayout())
{
    time_ = apvts.getRawParameterValue("time");
    speed_ = apvts.getRawParameterValue("speed");
    fdb_ = apvts.getRawParameterValue("fdb");
    input_ = apvts.getRawParameterValue("input");
    allocate(44100.0);
}

delay_params DelayAudioProcessor::currentParams() const
{
    return {time_->load(), speed_->load(), fdb_->load(), input_->load()};
}

void DelayAudioProcessor::allocate(double sampleRate)
{
    const auto sr = static_cast<float>(sampleRate);
    const auto frames = static_cast<size_t>(delay_buffer_frames(sr));
    memL_.assign(frames, 0.0f);
    memR_.assign(frames, 0.0f);
    delay_init(&core_, sr, memL_.data(), memR_.data());
    const auto p = currentParams();
    delay_set_params(&core_, &p);
    delay_reset(&core_);
}

void DelayAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    allocate(sampleRate);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

double DelayAudioProcessor::getTailLengthSeconds() const
{
    const auto p = currentParams();
    return static_cast<double>(delay_tail_seconds(&p));
}

bool DelayAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void DelayAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const auto p = currentParams();
    delay_set_params(&core_, &p);

    if (channels >= 2)
    {
        delay_process(&core_, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: run a second channel through scratch, keep the left result. The two channels are independent
        // in the core, so the scratch copy does not change the left output.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n)); // only if the host exceeds the block size it announced
        float* l = buffer.getWritePointer(0);
        std::copy(l, l + n, scratch_.begin());
        delay_process(&core_, l, scratch_.data(), n);
    }

    const float peak = delay_take_activity(&core_);
    float seen = activity_.load(std::memory_order_relaxed);
    while (peak > seen && !activity_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* DelayAudioProcessor::createEditor()
{
    return new DelayAudioProcessorEditor(*this);
}

void DelayAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DelayAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DelayAudioProcessor();
}
