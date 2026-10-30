#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kTone{"tone", 1};
const juce::ParameterID kTurns{"turns", 1};
const juce::ParameterID kDamp{"damp", 1};
const juce::ParameterID kLevel{"level", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SpringAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kTone, "Tone", NormalisableRange<float>(0.0f, 1.0f),
                                                      SPRING_DEFAULT_TONE));
    layout.add(std::make_unique<AudioParameterFloat>(kTurns, "Turns", NormalisableRange<float>(0.0f, 1.0f),
                                                      SPRING_DEFAULT_TURNS));
    layout.add(std::make_unique<AudioParameterFloat>(kDamp, "Damp", NormalisableRange<float>(0.0f, 1.0f),
                                                      SPRING_DEFAULT_DAMP));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f),
                                                      SPRING_DEFAULT_LEVEL));
    return layout;
}

SpringAudioProcessor::SpringAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SpringState", createLayout())
{
    tone_ = apvts.getRawParameterValue("tone");
    turns_ = apvts.getRawParameterValue("turns");
    damp_ = apvts.getRawParameterValue("damp");
    level_ = apvts.getRawParameterValue("level");
    spring_init(&core_, 44100.0f);
}

spring_params SpringAudioProcessor::readParams() const
{
    return spring_params{tone_->load(), turns_->load(), damp_->load(), level_->load()};
}

void SpringAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    spring_init(&core_, static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    const auto p = readParams();
    spring_set_params(&core_, &p);
    spring_reset(&core_);
}

bool SpringAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpringAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const auto p = readParams();
    spring_set_params(&core_, &p);

    if (channels >= 2)
    {
        spring_process(&core_, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: run a second channel through scratch, keep the left result. The tank sums to mono
        // before it starts, so left is the mono wet signal plus the dry one.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        float* l = buffer.getWritePointer(0);
        std::copy(l, l + n, scratch_.begin());
        spring_process(&core_, l, scratch_.data(), n);
    }

    const float peak = spring_take_activity(&core_);
    float seen = activity_.load(std::memory_order_relaxed);
    while (peak > seen && !activity_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* SpringAudioProcessor::createEditor()
{
    return new SpringAudioProcessorEditor(*this);
}

void SpringAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SpringAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpringAudioProcessor();
}
