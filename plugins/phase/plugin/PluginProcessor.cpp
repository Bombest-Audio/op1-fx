#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kA{"a", 1};
const juce::ParameterID kB{"b", 1};
const juce::ParameterID kR{"r", 1};
const juce::ParameterID kW{"w", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kBend{"bend", 1};

juce::NormalisableRange<float> timeRange(float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r(lo, hi);
    r.setSkewForCentre(centre);
    return r;
}

juce::String timeText(float seconds)
{
    return seconds < 1.0f ? juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms" : juce::String(seconds, 2) + " s";
}

static float parseTime(const juce::String& t)
{
    const float v = t.getFloatValue();
    return t.containsIgnoreCase("ms") ? v * 0.001f : v;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PhaseAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kA, "A", NormalisableRange<float>(0.0f, 1.0f), PHASE_DEFAULT_A));
    layout.add(std::make_unique<AudioParameterFloat>(kB, "B", NormalisableRange<float>(0.0f, 1.0f), PHASE_DEFAULT_B));
    layout.add(std::make_unique<AudioParameterFloat>(kR, "R", NormalisableRange<float>(0.0f, 1.0f), PHASE_DEFAULT_R));
    layout.add(std::make_unique<AudioParameterFloat>(kW, "W", NormalisableRange<float>(0.0f, 1.0f), PHASE_DEFAULT_W));
    layout.add(std::make_unique<AudioParameterFloat>(
        kAttack, "Attack", timeRange(0.001f, 5.0f, 0.15f), PHASE_DEFAULT_ATTACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return timeText(v); }).withValueFromStringFunction([](const juce::String& t) { return parseTime(t); })));
    layout.add(std::make_unique<AudioParameterFloat>(
        kDecay, "Decay", timeRange(0.005f, 5.0f, 0.35f), PHASE_DEFAULT_DECAY,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return timeText(v); }).withValueFromStringFunction([](const juce::String& t) { return parseTime(t); })));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      PHASE_DEFAULT_SUSTAIN));
    layout.add(std::make_unique<AudioParameterFloat>(
        kRelease, "Release", timeRange(0.005f, 8.0f, 0.4f), PHASE_DEFAULT_RELEASE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return timeText(v); }).withValueFromStringFunction([](const juce::String& t) { return parseTime(t); })));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f),
                                                      PHASE_DEFAULT_LEVEL));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 0, 12,
                                                    static_cast<int>(PHASE_DEFAULT_BEND_RANGE)));
    return layout;
}

PhaseAudioProcessor::PhaseAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PhaseState", createLayout())
{
    a_ = apvts.getRawParameterValue("a");
    b_ = apvts.getRawParameterValue("b");
    r_ = apvts.getRawParameterValue("r");
    w_ = apvts.getRawParameterValue("w");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_param_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    phase_init(&core_, 44100.0f);
}

double PhaseAudioProcessor::getTailLengthSeconds() const
{
    return static_cast<double>(release_->load());
}

void PhaseAudioProcessor::readParams(phase_params& p) const
{
    p.a = a_->load();
    p.b = b_->load();
    p.r = r_->load();
    p.w = w_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_param_->load();
    p.bend_range = bend_->load();
}

void PhaseAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    phase_init(&core_, static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    phase_params p;
    readParams(p);
    phase_set_params(&core_, &p);
    phase_reset(&core_);
}

bool PhaseAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void PhaseAudioProcessor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        phase_note_on(&core_, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        phase_note_off(&core_, m.getNoteNumber());
    else if (m.isPitchWheel())
        phase_pitch_bend(&core_, static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1)
            phase_mod_wheel(&core_, static_cast<float>(m.getControllerValue()) / 127.0f);
        else if (cc == 64)
            phase_sustain(&core_, m.getControllerValue() >= 64);
        else if (cc == 120 || cc == 123)
            phase_all_off(&core_);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        phase_all_off(&core_);
}

void PhaseAudioProcessor::renderRange(float* l, float* r, int start, int count)
{
    if (count > 0)
        phase_render(&core_, l + start, r + start, count);
}

void PhaseAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    buffer.clear();
    if (channels < 1 || n < 1)
        return;

    phase_params p;
    readParams(p);
    phase_set_params(&core_, &p);

    float* l = buffer.getWritePointer(0);
    float* r = nullptr;
    if (channels >= 2)
        r = buffer.getWritePointer(1);
    else
    {
        if (static_cast<int>(scratch_.size()) < n)
            scratch_.resize(static_cast<size_t>(n));
        r = scratch_.data();
    }

    // Render up to each MIDI event, apply it, continue. Notes start on the exact sample they arrive.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(pos, n, meta.samplePosition);
        renderRange(l, r, pos, at - pos);
        pos = at;
        handleMidi(meta.getMessage());
    }
    renderRange(l, r, pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = (l[i] + r[i]) * 0.70710678f; // the voices are centred around equal power, so this keeps a centred voice at its level

    int voices = 0;
    const float peak = phase_take_activity(&core_, &voices);
    voices_.store(voices, std::memory_order_relaxed);
    float seen = level_.load(std::memory_order_relaxed);
    while (peak > seen && !level_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
}

juce::AudioProcessorEditor* PhaseAudioProcessor::createEditor()
{
    return new PhaseAudioProcessorEditor(*this);
}

void PhaseAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PhaseAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhaseAudioProcessor();
}
