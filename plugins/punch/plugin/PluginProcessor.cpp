#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kFreq{"freq", 1};
const juce::ParameterID kPunch{"punch", 1};
const juce::ParameterID kRounds{"rounds", 1};
const juce::ParameterID kPower{"power", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PunchAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kFreq, "Freq", NormalisableRange<float>(0.0f, 1.0f),
                                                      PUNCH_DEFAULT_FREQ));
    layout.add(std::make_unique<AudioParameterFloat>(kPunch, "Punch", NormalisableRange<float>(0.0f, 1.0f),
                                                      PUNCH_DEFAULT_PUNCH));
    layout.add(std::make_unique<AudioParameterInt>(kRounds, "Rounds", 1, PUNCH_MAX_ROUNDS, PUNCH_DEFAULT_ROUNDS));
    layout.add(std::make_unique<AudioParameterFloat>(kPower, "Power", NormalisableRange<float>(0.0f, 1.0f),
                                                      PUNCH_DEFAULT_POWER));
    return layout;
}

PunchAudioProcessor::PunchAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PunchState", createLayout())
{
    freq_ = apvts.getRawParameterValue("freq");
    punch_ = apvts.getRawParameterValue("punch");
    rounds_ = apvts.getRawParameterValue("rounds");
    power_ = apvts.getRawParameterValue("power");
    punch_init(&core_, 44100.0f);
}

void PunchAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    punch_init(&core_, static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    punch_params p{freq_->load(), punch_->load(), static_cast<int>(rounds_->load() + 0.5f), power_->load()};
    punch_set_params(&core_, &p);
    punch_reset(&core_);
}

bool PunchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PunchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    punch_params p{freq_->load(), punch_->load(), static_cast<int>(rounds_->load() + 0.5f), power_->load()};
    punch_set_params(&core_, &p);

    if (channels >= 2)
    {
        punch_process(&core_, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: run a second channel through scratch, keep the left result.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        float* l = buffer.getWritePointer(0);
        std::copy(l, l + n, scratch_.begin());
        punch_process(&core_, l, scratch_.data(), n);
    }

    const float peak = punch_take_activity(&core_);
    float seen = activity_.load(std::memory_order_relaxed);
    while (peak > seen && !activity_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* PunchAudioProcessor::createEditor()
{
    return new PunchAudioProcessorEditor(*this);
}

void PunchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PunchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PunchAudioProcessor();
}
