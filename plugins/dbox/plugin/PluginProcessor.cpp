#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
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

juce::String decayText(float v, int)
{
    const float a = dbox_attack_seconds(v);
    return a > 0.0f ? "attack " + juce::String(juce::roundToInt(a * 1000.0f)) + " ms"
                    : juce::String(juce::roundToInt(dbox_decay_seconds(v) * 1000.0f)) + " ms";
}

float fromDecay(const juce::String& t)
{
    const auto text = t.trim().toLowerCase();
    const float ms = text.retainCharacters("0123456789.").getFloatValue();
    if (text.startsWith("attack"))
        return 0.8f + 0.2f * std::sqrt(juce::jlimit(0.0f, 1.0f, ms / 80.0f));
    return juce::jlimit(0.0f, 0.8f, 0.8f * std::log(juce::jmax(ms / 1000.0f, 0.05f) / 0.05f) / std::log(50.0f));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DBoxAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using Attr = juce::AudioParameterFloatAttributes;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"pitch", 1}, "Pitch", NormalisableRange<float>(-1.0f, 1.0f),
        DBOX_DEFAULT_PITCH, Attr().withStringFromValueFunction([](float v, int) {
            return (v >= 0 ? "+" : "") + juce::String(dbox_pitch_semitones(v), 1) + " st";
        }).withValueFromStringFunction([](const juce::String& t) { return juce::jlimit(-1.0f, 1.0f, t.getFloatValue() / 12.0f); })));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"wave", 1}, "Wave", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_WAVE, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"decay", 1}, "Decay", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_DECAY, Attr().withStringFromValueFunction(decayText).withValueFromStringFunction(fromDecay)));
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{"mode", 1}, "Mode", 0, DBOX_MODES - 1, DBOX_DEFAULT_MODE,
        juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(dbox_mode_name(v)); })
            .withValueFromStringFunction([](const juce::String& t) {
                for (int m = 0; m < DBOX_MODES; ++m)
                    if (t.trim().equalsIgnoreCase(dbox_mode_name(m)))
                        return m;
                return juce::jlimit(0, DBOX_MODES - 1, t.getIntValue());
            })));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"mix2", 1}, "Osc 2 Mix", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_MIX2, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"wave2", 1}, "Osc 2 Wave", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_WAVE2, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"decay2", 1}, "Osc 2 Decay", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_DECAY2, Attr().withStringFromValueFunction(decayText).withValueFromStringFunction(fromDecay)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"filter", 1}, "Filter", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_FILTER, Attr().withStringFromValueFunction([](float v, int) {
            const float hz = dbox_filter_hz(v);
            return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
        }).withValueFromStringFunction([](const juce::String& t) {
            const auto low = t.trim().toLowerCase();
            float hz = low.retainCharacters("0123456789.").getFloatValue();
            if (low.contains("k"))
                hz *= 1000.0f;
            hz = juce::jlimit(60.0f, 20000.0f, hz);
            const float x = std::log(hz / 20000.0f) / std::log(60.0f / 20000.0f);
            return std::pow(juce::jlimit(0.0f, 1.0f, x), 1.0f / 0.8f);
        })));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"env_atk", 1}, "Env Attack",
        NormalisableRange<float>(0.0005f, 5.0f, 0.0f, 0.3f), DBOX_DEFAULT_ATTACK,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"env_dec", 1}, "Env Decay",
        NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f), DBOX_DEFAULT_ENV_DECAY,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"env_sus", 1}, "Env Sustain", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_SUSTAIN, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"env_rel", 1}, "Env Release",
        NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f), DBOX_DEFAULT_RELEASE,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"level", 1}, "Level", NormalisableRange<float>(0.0f, 1.0f),
        DBOX_DEFAULT_LEVEL, Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{"bend", 1}, "Bend Range", 1, 12, 2));
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{"root", 1}, "Root Note", 0, 103, DBOX_DEFAULT_ROOT));
    return layout;
}

DBoxAudioProcessor::DBoxAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DBoxState", createLayout())
{
    pitch_ = apvts.getRawParameterValue("pitch");
    wave_ = apvts.getRawParameterValue("wave");
    decay_ = apvts.getRawParameterValue("decay");
    mode_ = apvts.getRawParameterValue("mode");
    mix2_ = apvts.getRawParameterValue("mix2");
    wave2_ = apvts.getRawParameterValue("wave2");
    decay2_ = apvts.getRawParameterValue("decay2");
    filter_ = apvts.getRawParameterValue("filter");
    atk_ = apvts.getRawParameterValue("env_atk");
    dec_ = apvts.getRawParameterValue("env_dec");
    sus_ = apvts.getRawParameterValue("env_sus");
    release_ = apvts.getRawParameterValue("env_rel");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    root_ = apvts.getRawParameterValue("root");
    dbox_init(&core_, 44100.0f);
}

dbox_params DBoxAudioProcessor::readParams() const
{
    dbox_params p;
    p.pitch = pitch_->load();
    p.wave = wave_->load();
    p.decay = decay_->load();
    p.mode = static_cast<int>(mode_->load() + 0.5f);
    p.mix2 = mix2_->load();
    p.wave2 = wave2_->load();
    p.decay2 = decay2_->load();
    p.filter = filter_->load();
    p.attack = atk_->load();
    p.env_decay = dec_->load();
    p.sustain = sus_->load();
    p.release = release_->load();
    p.level = level_->load();
    p.bend_range = bend_->load();
    p.root = static_cast<int>(root_->load() + 0.5f);
    return p;
}

void DBoxAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dbox_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    dbox_set_params(&core_, &p);
    dbox_reset(&core_);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DBoxAudioProcessor::reset()
{
    dbox_reset(&core_);
}

bool DBoxAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DBoxAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    dbox_set_params(&core_, &params);

    if (static_cast<int>(scratch_.size()) < n)
        scratch_.resize(static_cast<size_t>(n));

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
            dbox_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            dbox_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            dbox_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            dbox_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                dbox_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                dbox_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                dbox_all_off(&core_);
        }
    }
    if (pos < n)
        dbox_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    dbox_activity act;
    dbox_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
    keys_.fetch_or(act.keys, std::memory_order_relaxed);
}

DBoxAudioProcessor::Activity DBoxAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    a.keys = keys_.exchange(0u);
    return a;
}

juce::AudioProcessorEditor* DBoxAudioProcessor::createEditor()
{
    return new DBoxAudioProcessorEditor(*this);
}

void DBoxAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DBoxAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DBoxAudioProcessor();
}
