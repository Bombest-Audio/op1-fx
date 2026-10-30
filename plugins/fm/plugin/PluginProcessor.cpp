#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kRatio{"ratio", 1};
const juce::ParameterID kIndex{"index", 1};
const juce::ParameterID kFeedback{"feedback", 1};
const juce::ParameterID kLayer{"layer", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kBend{"bend", 1};

juce::String percent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)); }

// Text typed or read back by a host goes the other way: "55" is 0.55, "250 ms" and "0.25 s" are both 0.25.
float fromPercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }

float fromSeconds(const juce::String& t)
{
    const auto text = t.trim().toLowerCase();
    const float v = text.getFloatValue();
    return text.endsWith("ms") ? v / 1000.0f : v;
}

float fromRatio(const juce::String& t)
{
    // fm_ratio_value() rises with its input, so find the knob position that gives the typed ratio by bisection.
    const float target = t.getFloatValue();
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (fm_ratio_value(mid) < target ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

juce::String seconds(float v, int)
{
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout FmAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kRatio, "Ratio", NormalisableRange<float>(0.0f, 1.0f), FM_DEFAULT_RATIO,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return juce::String(fm_ratio_value(v), 2);
        }).withValueFromStringFunction(fromRatio)));
    layout.add(std::make_unique<AudioParameterFloat>(kIndex, "Index", NormalisableRange<float>(0.0f, 1.0f), FM_DEFAULT_INDEX,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kFeedback, "Feedback", NormalisableRange<float>(0.0f, 1.0f),
                                                      FM_DEFAULT_FEEDBACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kLayer, "Layer", NormalisableRange<float>(0.0f, 1.0f), FM_DEFAULT_LAYER,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", NormalisableRange<float>(0.0005f, 5.0f, 0.0f, 0.3f),
                                                      FM_DEFAULT_ATTACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f),
                                                      FM_DEFAULT_DECAY,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      FM_DEFAULT_SUSTAIN,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f),
                                                      FM_DEFAULT_RELEASE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), FM_DEFAULT_LEVEL,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    return layout;
}

FmAudioProcessor::FmAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "FmState", createLayout())
{
    ratio_ = apvts.getRawParameterValue("ratio");
    index_ = apvts.getRawParameterValue("index");
    feedback_ = apvts.getRawParameterValue("feedback");
    layer_ = apvts.getRawParameterValue("layer");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    fm_init(&core_, 44100.0f);
}

fm_params FmAudioProcessor::readParams() const
{
    // The engine extras (mod decay, detune, velocity to index, key tracking) stay at the OP-1 factory values.
    fm_params p;
    fm_default_params(&p);
    p.ratio = ratio_->load();
    p.index = index_->load();
    p.feedback = feedback_->load();
    p.layer = layer_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_->load();
    p.bend_range = bend_->load();
    return p;
}

void FmAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    fm_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    fm_set_params(&core_, &p);
    fm_reset(&core_);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void FmAudioProcessor::reset()
{
    fm_reset(&core_);
}

bool FmAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void FmAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    fm_set_params(&core_, &params);

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
            fm_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            fm_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            fm_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            fm_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                fm_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                fm_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                fm_all_off(&core_);
        }
    }
    if (pos < n)
        fm_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    fm_activity act;
    fm_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
}

FmAudioProcessor::Activity FmAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* FmAudioProcessor::createEditor()
{
    return new FmAudioProcessorEditor(*this);
}

void FmAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void FmAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FmAudioProcessor();
}
