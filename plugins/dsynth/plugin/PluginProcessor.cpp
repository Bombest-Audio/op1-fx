#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kMod{"mod", 1};
const juce::ParameterID kWave{"wave", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kFilter{"filter", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kAmpDecay{"ampdecay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kBend{"bend", 1};

juce::String percent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)); }
float fromPercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }

float fromSeconds(const juce::String& t)
{
    const auto text = t.trim().toLowerCase();
    const float v = text.getFloatValue();
    return text.endsWith("ms") ? v / 1000.0f : v;
}

juce::String seconds(float v, int)
{
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}

// Inverse of DSynthAudioProcessor_decayText: "ATK 120 ms" is in the upper half, a plain time in the lower half.
// Both mappings rise with the knob, so bisect.
float decayFromText(const juce::String& t)
{
    const auto text = t.trim();
    const bool attack = text.toUpperCase().startsWith("ATK");
    // fromFirstOccurrenceOf returns an empty string when the marker is absent, so only strip it for attack text.
    const float target = fromSeconds(attack ? text.substring(3) : text);
    float lo = attack ? 0.5f : 0.0f, hi = attack ? 1.0f : 0.5f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        const float v = attack ? dsynth_attack_seconds(mid) : dsynth_decay_seconds(mid);
        (v < target ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

} // namespace

juce::String DSynthAudioProcessor_decayText(float knob)
{
    // Below the middle the knob sets the decay, above it the decay stays long and the attack grows.
    if (knob <= 0.5f)
        return seconds(dsynth_decay_seconds(knob), 0);
    return "ATK " + seconds(dsynth_attack_seconds(knob), 0);
}

juce::AudioProcessorValueTreeState::ParameterLayout DSynthAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using Attr = juce::AudioParameterFloatAttributes;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kMod, "Mod", NormalisableRange<float>(0.0f, 1.0f), DSYNTH_DEFAULT_MOD,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kWave, "Wave", NormalisableRange<float>(0.0f, 1.0f), DSYNTH_DEFAULT_WAVE,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", NormalisableRange<float>(0.0f, 1.0f), DSYNTH_DEFAULT_DECAY,
        Attr().withStringFromValueFunction([](float v, int) { return DSynthAudioProcessor_decayText(v); })
            .withValueFromStringFunction(decayFromText)));
    layout.add(std::make_unique<AudioParameterInt>(kFilter, "Filter", 0, DSYNTH_FILTER_MODES - 1, DSYNTH_DEFAULT_FILTER_MODE,
        juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) {
            return juce::String(dsynth_filter_name(v));
        }).withValueFromStringFunction([](const juce::String& t) {
            for (int m = 0; m < DSYNTH_FILTER_MODES; ++m)
                if (t.trim().equalsIgnoreCase(dsynth_filter_name(m)))
                    return m;
            return juce::jlimit(0, DSYNTH_FILTER_MODES - 1, t.getIntValue());
        })));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Amp Attack", NormalisableRange<float>(0.0005f, 5.0f, 0.0001f, 0.3f),
        DSYNTH_DEFAULT_AMP_ATTACK, Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kAmpDecay, "Amp Decay", NormalisableRange<float>(0.005f, 10.0f, 0.0001f, 0.3f),
        DSYNTH_DEFAULT_AMP_DECAY, Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Amp Sustain", NormalisableRange<float>(0.0f, 1.0f),
        DSYNTH_DEFAULT_AMP_SUSTAIN, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Amp Release", NormalisableRange<float>(0.005f, 10.0f, 0.0001f, 0.3f),
        DSYNTH_DEFAULT_AMP_RELEASE, Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), DSYNTH_DEFAULT_LEVEL,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    return layout;
}

DSynthAudioProcessor::DSynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DSynthState", createLayout())
{
    mod_ = apvts.getRawParameterValue("mod");
    wave_ = apvts.getRawParameterValue("wave");
    decay_ = apvts.getRawParameterValue("decay");
    filter_ = apvts.getRawParameterValue("filter");
    attack_ = apvts.getRawParameterValue("attack");
    ampDecay_ = apvts.getRawParameterValue("ampdecay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    dsynth_init(&core_, 44100.0f);
}

double DSynthAudioProcessor::getTailLengthSeconds() const
{
    // A note rings for its decay or the amp release, whichever the sound is still using; report the longer bound.
    return release_ != nullptr ? static_cast<double>(juce::jmax(release_->load(), 2.0f)) : 0.0;
}

dsynth_params DSynthAudioProcessor::readParams() const
{
    // The engine extras (oscillator 2 pitch, wave and decay, filter cutoff) stay at the OP-1 factory values.
    dsynth_params p;
    dsynth_default_params(&p);
    p.mod = mod_->load();
    p.wave = wave_->load();
    p.decay = decay_->load();
    p.filter_mode = static_cast<int>(filter_->load() + 0.5f);
    p.amp_attack = attack_->load();
    p.amp_decay = ampDecay_->load();
    p.amp_sustain = sustain_->load();
    p.amp_release = release_->load();
    p.level = level_->load();
    p.bend_range = bend_->load();
    return p;
}

void DSynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dsynth_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    dsynth_set_params(&core_, &p);
    dsynth_reset(&core_);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DSynthAudioProcessor::reset()
{
    dsynth_reset(&core_);
}

bool DSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    dsynth_set_params(&core_, &params);

    if (static_cast<int>(scratch_.size()) < n)
        scratch_.resize(static_cast<size_t>(n)); // only if a host exceeds the size it announced

    float* l = buffer.getWritePointer(0);
    float* r = channels >= 2 ? buffer.getWritePointer(1) : scratch_.data();

    // Sample-accurate notes: render up to each event, apply it, carry on. The core output does not depend on
    // how the block is split.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(pos, n, meta.samplePosition);
        if (at > pos)
        {
            dsynth_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            dsynth_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            dsynth_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            dsynth_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                dsynth_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                dsynth_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                dsynth_all_off(&core_);
        }
    }
    if (pos < n)
        dsynth_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    dsynth_activity act;
    dsynth_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
}

DSynthAudioProcessor::Activity DSynthAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* DSynthAudioProcessor::createEditor()
{
    return new DSynthAudioProcessorEditor(*this);
}

void DSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DSynthAudioProcessor();
}
