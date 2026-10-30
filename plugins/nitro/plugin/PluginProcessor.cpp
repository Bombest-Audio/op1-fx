#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kFreq{"freq", 1};
const juce::ParameterID kRes{"res", 1};
const juce::ParameterID kShape{"shape", 1};
const juce::ParameterID kDrive{"drive", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout NitroAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kFreq, "Freq", NormalisableRange<float>(0.0f, 1.0f),
                                                      NITRO_DEFAULT_FREQ));
    layout.add(std::make_unique<AudioParameterFloat>(kRes, "Res", NormalisableRange<float>(0.0f, 1.0f),
                                                      NITRO_DEFAULT_RES));
    layout.add(std::make_unique<AudioParameterFloat>(kShape, "Shape", NormalisableRange<float>(0.0f, 1.0f),
                                                      NITRO_DEFAULT_SHAPE));
    layout.add(std::make_unique<AudioParameterFloat>(kDrive, "Drive", NormalisableRange<float>(0.0f, 1.0f),
                                                      NITRO_DEFAULT_DRIVE));
    return layout;
}

NitroAudioProcessor::NitroAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NitroState", createLayout())
{
    freq_ = apvts.getRawParameterValue("freq");
    res_ = apvts.getRawParameterValue("res");
    shape_ = apvts.getRawParameterValue("shape");
    drive_ = apvts.getRawParameterValue("drive");
    nitro_init(&core_, 44100.0f);
}

nitro_params NitroAudioProcessor::currentParams() const noexcept
{
    return {freq_->load(), res_->load(), shape_->load(), drive_->load()};
}

void NitroAudioProcessor::prepareToPlay(double sampleRate, int)
{
    nitro_init(&core_, static_cast<float>(sampleRate));
    const auto p = currentParams();
    nitro_set_params(&core_, &p);
    nitro_reset(&core_);
}

bool NitroAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void NitroAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const auto p = currentParams();
    nitro_set_params(&core_, &p);

    if (channels >= 2)
        nitro_process(&core_, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    else if (channels == 1)
        nitro_process(&core_, buffer.getWritePointer(0), nullptr, n);

    const float level = nitro_take_activity(&core_);
    float seen = activity_.load(std::memory_order_relaxed);
    while (level > seen && !activity_.compare_exchange_weak(seen, level, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* NitroAudioProcessor::createEditor()
{
    return new NitroAudioProcessorEditor(*this);
}

void NitroAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void NitroAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NitroAudioProcessor();
}
