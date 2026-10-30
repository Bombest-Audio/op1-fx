#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kTone{"tone", 1};
const juce::ParameterID kPhonic{"phonic", 1};
const juce::ParameterID kBaud{"baud", 1};
const juce::ParameterID kTelematic{"telematic", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PhoneAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kTone, "Tone", NormalisableRange<float>(0.0f, 1.0f),
                                                      PHONE_DEFAULT_TONE));
    layout.add(std::make_unique<AudioParameterInt>(kPhonic, "Phonic", 1, PHONE_MAX_CYCLES, PHONE_DEFAULT_PHONIC));
    layout.add(std::make_unique<AudioParameterInt>(kBaud, "Baud", 1, PHONE_MAX_REPEATS, PHONE_DEFAULT_BAUD));
    layout.add(std::make_unique<AudioParameterFloat>(kTelematic, "Telematic", NormalisableRange<float>(0.0f, 1.0f),
                                                      PHONE_DEFAULT_TELEMATIC));
    return layout;
}

PhoneAudioProcessor::PhoneAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PhoneState", createLayout()),
      core_(new phone_t)
{
    tone_ = apvts.getRawParameterValue("tone");
    phonic_ = apvts.getRawParameterValue("phonic");
    baud_ = apvts.getRawParameterValue("baud");
    telematic_ = apvts.getRawParameterValue("telematic");
    phone_init(core_.get(), 44100.0f);
}

phone_params PhoneAudioProcessor::readParams() const
{
    return phone_params{tone_->load(), static_cast<int>(phonic_->load() + 0.5f), static_cast<int>(baud_->load() + 0.5f),
                        telematic_->load()};
}

void PhoneAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    phone_init(core_.get(), static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    const auto p = readParams();
    phone_set_params(core_.get(), &p);
    phone_reset(core_.get());
}

bool PhoneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PhoneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const auto p = readParams();
    phone_set_params(core_.get(), &p);

    if (channels >= 2)
    {
        phone_process(core_.get(), buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: run a copy as the second channel and keep the left result.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        float* l = buffer.getWritePointer(0);
        std::copy(l, l + n, scratch_.begin());
        phone_process(core_.get(), l, scratch_.data(), n);
    }

    const float level = phone_take_activity(core_.get());
    float seen = activity_.load(std::memory_order_relaxed);
    while (level > seen && !activity_.compare_exchange_weak(seen, level, std::memory_order_relaxed)) {}
    swaps_.fetch_add(phone_take_swaps(core_.get()), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PhoneAudioProcessor::createEditor()
{
    return new PhoneAudioProcessorEditor(*this);
}

void PhoneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PhoneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhoneAudioProcessor();
}
