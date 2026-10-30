#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kOscs{"oscs", 1};
const juce::ParameterID kFeedback{"feedback", 1};
const juce::ParameterID kSpread{"spread", 1};
const juce::ParameterID kWander{"wander", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kBend{"bend", 1};

juce::String timeText(float norm)
{
    const float s = cluster_env_time(norm);
    return s < 1.0f ? juce::String(juce::roundToInt(s * 1000.0f)) + " ms" : juce::String(s, 2) + " s";
}

// Inverse of timeText: "250 ms" or "1.5 s" (a bare number is read as seconds) back to the normalized value.
float timeValue(const juce::String& text)
{
    const double n = text.getDoubleValue();
    const double seconds = text.containsIgnoreCase("ms") ? n * 0.001 : n;
    return juce::jlimit(0.0f, 1.0f, cluster_env_norm(static_cast<float>(juce::jmax(seconds, 1.0e-6))));
}

// Percent text maps to 0..1; wander shows one decimal because its useful range is small.
float percentValue(const juce::String& text)
{
    return juce::jlimit(0.0f, 1.0f, static_cast<float>(text.getDoubleValue() * 0.01));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ClusterAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const NormalisableRange<float> unit(0.0f, 1.0f);
    auto percent = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
    auto timeFn = [](float v, int) { return timeText(v); };
    auto timeIn = [](const juce::String& t) { return timeValue(t); };
    auto percentIn = [](const juce::String& t) { return percentValue(t); };

    layout.add(std::make_unique<AudioParameterInt>(kOscs, "Oscs", 1, CLUSTER_OSCS, CLUSTER_DEFAULT_OSCS));
    layout.add(std::make_unique<AudioParameterFloat>(kFeedback, "Feedback", unit, CLUSTER_DEFAULT_FEEDBACK,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(percentIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kSpread, "Spread", unit, CLUSTER_DEFAULT_SPREAD,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(percentIn)));
    layout.add(std::make_unique<AudioParameterFloat>(
        kWander, "Wander", unit, CLUSTER_DEFAULT_WANDER,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v * 100.0f, 1) + " %"; })
            .withValueFromStringFunction(percentIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", unit, CLUSTER_DEFAULT_ATTACK,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(timeFn).withValueFromStringFunction(timeIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", unit, CLUSTER_DEFAULT_DECAY,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(timeFn).withValueFromStringFunction(timeIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", unit, CLUSTER_DEFAULT_SUSTAIN,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(percentIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", unit, CLUSTER_DEFAULT_RELEASE,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(timeFn).withValueFromStringFunction(timeIn)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", unit, CLUSTER_DEFAULT_LEVEL,
                                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(percentIn)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 0, 12, static_cast<int>(CLUSTER_DEFAULT_BEND)));
    return layout;
}

ClusterAudioProcessor::ClusterAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ClusterState", createLayout())
{
    oscs_ = apvts.getRawParameterValue("oscs");
    feedback_ = apvts.getRawParameterValue("feedback");
    spread_ = apvts.getRawParameterValue("spread");
    wander_ = apvts.getRawParameterValue("wander");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_param_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    for (auto& a : activity_)
        a.store(0.0f);
    cluster_init(&core_, 44100.0f);
}

cluster_params ClusterAudioProcessor::readParams() const
{
    cluster_params p;
    p.oscs = static_cast<int>(oscs_->load() + 0.5f);
    p.feedback = feedback_->load();
    p.spread = spread_->load();
    p.wander = wander_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_param_->load();
    p.bend_range = bend_->load();
    return p;
}

void ClusterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    cluster_init(&core_, static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)) * 2u, 0.0f);
    const auto p = readParams();
    cluster_set_params(&core_, &p);
}

bool ClusterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ClusterAudioProcessor::applyMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        cluster_note_on(&core_, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        cluster_note_off(&core_, m.getNoteNumber());
    else if (m.isPitchWheel())
        cluster_pitch_bend(&core_, static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const int v = m.getControllerValue();
        if (cc == 1)
            cluster_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
        else if (cc == 64)
            cluster_sustain(&core_, v >= 64);
        else if (cc == 120 || cc == 123)
            cluster_all_off(&core_);
        else if (cc == 121)
        {
            cluster_mod_wheel(&core_, 0.0f);
            cluster_pitch_bend(&core_, 0.0f);
            cluster_sustain(&core_, 0);
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        cluster_all_off(&core_);
}

void ClusterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (n <= 0 || channels <= 0)
        return;

    const auto p = readParams();
    cluster_set_params(&core_, &p);

    if (static_cast<int>(scratch_.size()) < 2 * n)
        scratch_.resize(static_cast<size_t>(2 * n)); // only if the host exceeds the block size it announced

    float* left = channels >= 2 ? buffer.getWritePointer(0) : scratch_.data();
    float* right = channels >= 2 ? buffer.getWritePointer(1) : scratch_.data() + n;

    // Render up to each event, apply it, carry on: notes start on the exact sample.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(pos, n, meta.samplePosition);
        if (at > pos)
        {
            cluster_render(&core_, left + pos, right + pos, at - pos);
            pos = at;
        }
        applyMidi(meta.getMessage());
    }
    if (pos < n)
        cluster_render(&core_, left + pos, right + pos, n - pos);

    if (channels == 1)
    {
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            out[i] = 0.5f * (left[i] + right[i]);
    }
    for (int ch = 2; ch < channels; ++ch)
        buffer.clear(ch, 0, n);

    // meters for the editor: raise, never lower, the shared values
    const float peak = cluster_take_level(&core_);
    float seen = level_.load(std::memory_order_relaxed);
    while (peak > seen && !level_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
    float act[CLUSTER_OSCS];
    cluster_take_activity(&core_, act);
    for (int i = 0; i < CLUSTER_OSCS; ++i)
    {
        float s = activity_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        while (act[i] > s && !activity_[static_cast<size_t>(i)].compare_exchange_weak(s, act[i], std::memory_order_relaxed)) {}
    }
}

void ClusterAudioProcessor::takeActivity(std::array<float, CLUSTER_OSCS>& out) noexcept
{
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = activity_[i].exchange(0.0f);
}

juce::AudioProcessorEditor* ClusterAudioProcessor::createEditor()
{
    return new ClusterAudioProcessorEditor(*this);
}

void ClusterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ClusterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClusterAudioProcessor();
}
