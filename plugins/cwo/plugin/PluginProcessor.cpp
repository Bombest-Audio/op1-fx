#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kFreq{"freq", 1};
const juce::ParameterID kDelay{"delay", 1};
const juce::ParameterID kFdback{"fdback", 1};
const juce::ParameterID kSideband{"sideband", 1};
const juce::ParameterID kMix{"mix", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout CwoAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kFreq, "Freq", NormalisableRange<float>(0.0f, 1.0f),
                                                      CWO_DEFAULT_FREQ));
    layout.add(std::make_unique<AudioParameterFloat>(kDelay, "Delay", NormalisableRange<float>(0.0f, 1.0f),
                                                      CWO_DEFAULT_DELAY));
    layout.add(std::make_unique<AudioParameterFloat>(kFdback, "Fdback", NormalisableRange<float>(0.0f, 1.0f),
                                                      CWO_DEFAULT_FDBACK));
    layout.add(std::make_unique<AudioParameterFloat>(kSideband, "Sideband", NormalisableRange<float>(0.0f, 1.0f),
                                                      CWO_DEFAULT_SIDEBAND));
    layout.add(std::make_unique<AudioParameterFloat>(kMix, "Mix", NormalisableRange<float>(0.0f, 1.0f),
                                                      CWO_DEFAULT_MIX));
    return layout;
}

CwoAudioProcessor::CwoAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CwoState", createLayout()),
      core_(std::make_unique<cwo_t>())
{
    freq_ = apvts.getRawParameterValue("freq");
    delay_ = apvts.getRawParameterValue("delay");
    fdback_ = apvts.getRawParameterValue("fdback");
    sideband_ = apvts.getRawParameterValue("sideband");
    mix_ = apvts.getRawParameterValue("mix");
    cwo_init(core_.get(), 44100.0f);
}

void CwoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    cwo_init(core_.get(), static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    cwo_params p{freq_->load(), delay_->load(), fdback_->load(), sideband_->load(), mix_->load()};
    cwo_set_params(core_.get(), &p);
    cwo_reset(core_.get());
}

bool CwoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CwoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    cwo_params p{freq_->load(), delay_->load(), fdback_->load(), sideband_->load(), mix_->load()};
    cwo_set_params(core_.get(), &p);

    if (channels >= 2)
    {
        cwo_process(core_.get(), buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: run a second channel through scratch, keep the left result.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        float* l = buffer.getWritePointer(0);
        std::fill(scratch_.begin(), scratch_.begin() + n, 0.0f);
        cwo_process(core_.get(), l, scratch_.data(), n);
    }

    const float peak = cwo_take_activity(core_.get());
    float seen = activity_.load(std::memory_order_relaxed);
    while (peak > seen && !activity_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* CwoAudioProcessor::createEditor()
{
    return new CwoAudioProcessorEditor(*this);
}

void CwoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void CwoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CwoAudioProcessor();
}
