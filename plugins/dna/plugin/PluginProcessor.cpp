#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kMutate{"mutate", 1};
const juce::ParameterID kBits{"bits", 1};
const juce::ParameterID kSub{"sub", 1};
const juce::ParameterID kGrit{"grit", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kBend{"bend", 1};

juce::String percent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)); }
float fromPercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }

juce::String bitsText(float v, int) { return juce::String(juce::roundToInt(dna_bits_value(v))); }
float bitsFromText(const juce::String& t) { return (t.getFloatValue() - 2.0f) / 126.0f; }

juce::String seconds(float v, int)
{
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}

float fromSeconds(const juce::String& t)
{
    const auto text = t.trim().toLowerCase();
    const float v = text.getFloatValue();
    return text.endsWith("ms") ? v / 1000.0f : v;
}

// Envelope times are stored as a plain 0..1 knob position, not as seconds, so a saved state reloads to exactly the same
// float (an exponential NormalisableRange does not round-trip bit for bit). dna_time_seconds() maps the knob to 1 ms..5 s.
juce::String timeText(float v, int) { return seconds(dna_time_seconds(v), 0); }
float timeFromText(const juce::String& t) { return dna_time_norm(fromSeconds(t)); }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DnaAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using Attr = juce::AudioParameterFloatAttributes;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kMutate, "Mutate", NormalisableRange<float>(-1.0f, 1.0f), DNA_DEFAULT_MUTATE,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kBits, "Bits", NormalisableRange<float>(0.0f, 1.0f), DNA_DEFAULT_BITS,
        Attr().withStringFromValueFunction(bitsText).withValueFromStringFunction(bitsFromText)));
    layout.add(std::make_unique<AudioParameterFloat>(kSub, "Sub", NormalisableRange<float>(0.0f, 1.0f), DNA_DEFAULT_SUB,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kGrit, "Grit", NormalisableRange<float>(0.0f, 1.0f), DNA_DEFAULT_GRIT,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", NormalisableRange<float>(0.0f, 1.0f), dna_time_norm(DNA_DEFAULT_ATTACK),
        Attr().withStringFromValueFunction(timeText).withValueFromStringFunction(timeFromText)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", NormalisableRange<float>(0.0f, 1.0f), dna_time_norm(DNA_DEFAULT_DECAY),
        Attr().withStringFromValueFunction(timeText).withValueFromStringFunction(timeFromText)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f), DNA_DEFAULT_SUSTAIN,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", NormalisableRange<float>(0.0f, 1.0f), dna_time_norm(DNA_DEFAULT_RELEASE),
        Attr().withStringFromValueFunction(timeText).withValueFromStringFunction(timeFromText)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), DNA_DEFAULT_LEVEL,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    return layout;
}

DnaAudioProcessor::DnaAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DnaState", createLayout())
{
    mutate_ = apvts.getRawParameterValue("mutate");
    bits_ = apvts.getRawParameterValue("bits");
    sub_ = apvts.getRawParameterValue("sub");
    grit_ = apvts.getRawParameterValue("grit");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    dna_init(&core_, 44100.0f);
}

dna_params DnaAudioProcessor::readParams() const
{
    dna_params p;
    dna_default_params(&p);
    p.mutate = mutate_->load();
    p.bits = bits_->load();
    p.sub = sub_->load();
    p.grit = grit_->load();
    p.attack = dna_time_seconds(attack_->load());
    p.decay = dna_time_seconds(decay_->load());
    p.sustain = sustain_->load();
    p.release = dna_time_seconds(release_->load());
    p.level = level_->load();
    p.bend_range = bend_->load();
    return p;
}

void DnaAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dna_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    dna_set_params(&core_, &p);
    dna_reset(&core_);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DnaAudioProcessor::reset()
{
    dna_reset(&core_);
}

bool DnaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DnaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    dna_set_params(&core_, &params);

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
            dna_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            dna_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            dna_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            dna_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                dna_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                dna_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                dna_all_off(&core_);
        }
    }
    if (pos < n)
        dna_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    dna_activity act;
    dna_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
}

DnaAudioProcessor::Activity DnaAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* DnaAudioProcessor::createEditor()
{
    return new DnaAudioProcessorEditor(*this);
}

void DnaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DnaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DnaAudioProcessor();
}
