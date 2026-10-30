#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kX{"x", 1};
const juce::ParameterID kY{"y", 1};
const juce::ParameterID kZfb{"zfb", 1};
const juce::ParameterID kMix{"mix", 1};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout GridAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kX, "X", NormalisableRange<float>(0.0f, 1.0f), GRID_DEFAULT_X));
    layout.add(std::make_unique<AudioParameterFloat>(kY, "Y", NormalisableRange<float>(0.0f, 1.0f), GRID_DEFAULT_Y));
    layout.add(std::make_unique<AudioParameterFloat>(kZfb, "Z FDB", NormalisableRange<float>(0.0f, 1.0f),
                                                      GRID_DEFAULT_ZFB));
    layout.add(std::make_unique<AudioParameterFloat>(kMix, "Mix", NormalisableRange<float>(0.0f, 1.0f),
                                                      GRID_DEFAULT_MIX));
    return layout;
}

GridAudioProcessor::GridAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GridState", createLayout())
{
    x_ = apvts.getRawParameterValue("x");
    y_ = apvts.getRawParameterValue("y");
    zfb_ = apvts.getRawParameterValue("zfb");
    mix_ = apvts.getRawParameterValue("mix");
    startCore(44100.0);
}

grid_params GridAudioProcessor::currentParams() const noexcept
{
    return grid_params{x_->load(), y_->load(), zfb_->load(), mix_->load()};
}

void GridAudioProcessor::startCore(double sampleRate)
{
    mesh_.assign(grid_memory_floats(static_cast<float>(sampleRate)), 0.0f);
    grid_init(&core_, static_cast<float>(sampleRate), mesh_.data());
    const auto p = currentParams();
    grid_set_params(&core_, &p);
    grid_reset(&core_);
}

void GridAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    startCore(sampleRate);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

bool GridAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void GridAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();

    const auto p = currentParams();
    grid_set_params(&core_, &p);

    if (channels >= 2)
    {
        grid_process(&core_, buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    }
    else if (channels == 1)
    {
        // Mono: the core wants two buffers. Run a copy through the second mesh and keep the left result.
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        float* l = buffer.getWritePointer(0);
        std::copy(l, l + n, scratch_.begin());
        grid_process(&core_, l, scratch_.data(), n);
    }

    const float peak = grid_take_activity(&core_);
    float seen = activity_.load(std::memory_order_relaxed);
    while (peak > seen && !activity_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* GridAudioProcessor::createEditor()
{
    return new GridAudioProcessorEditor(*this);
}

void GridAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void GridAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GridAudioProcessor();
}
