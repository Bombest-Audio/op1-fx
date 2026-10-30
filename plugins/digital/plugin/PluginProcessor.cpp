#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kFeedback{"feedback", 1};
const juce::ParameterID kRatio{"ratio", 1};
const juce::ParameterID kDetune{"detune", 1};
const juce::ParameterID kCrunch{"crunch", 1};
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

float fromOctaveStep(const juce::String& t)
{
    // The knob reads out as an octave step, -3 to 4. Find the knob position in the middle of the typed step.
    const int target = juce::jlimit(-3, 4, t.getIntValue());
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (digital_octave_step(mid) < target ? lo : hi) = mid;
    }
    return juce::jmin(1.0f, lo + 0.5f / 8.0f * 0.5f);
}

juce::String signedPercent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)); }

juce::String seconds(float v, int)
{
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DigitalAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kFeedback, "Feedback", NormalisableRange<float>(0.0f, 1.0f),
                                                      DIGITAL_DEFAULT_FEEDBACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRatio, "Ratio", NormalisableRange<float>(0.0f, 1.0f),
                                                      DIGITAL_DEFAULT_RATIO,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return juce::String(digital_octave_step(v));
        }).withValueFromStringFunction(fromOctaveStep)));
    layout.add(std::make_unique<AudioParameterFloat>(kDetune, "Detune", NormalisableRange<float>(-1.0f, 1.0f),
                                                      DIGITAL_DEFAULT_DETUNE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(signedPercent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kCrunch, "Crunch", NormalisableRange<float>(0.0f, 1.0f),
                                                      DIGITAL_DEFAULT_CRUNCH,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", NormalisableRange<float>(0.0005f, 5.0f, 0.0f, 0.3f),
                                                      DIGITAL_DEFAULT_ATTACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f),
                                                      DIGITAL_DEFAULT_DECAY,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      DIGITAL_DEFAULT_SUSTAIN,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f),
                                                      DIGITAL_DEFAULT_RELEASE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), DIGITAL_DEFAULT_LEVEL,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    return layout;
}

DigitalAudioProcessor::DigitalAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DigitalState", createLayout())
{
    feedback_ = apvts.getRawParameterValue("feedback");
    ratio_ = apvts.getRawParameterValue("ratio");
    detune_ = apvts.getRawParameterValue("detune");
    crunch_ = apvts.getRawParameterValue("crunch");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    digital_init(&core_, 44100.0f);
}

digital_params DigitalAudioProcessor::readParams() const
{
    digital_params p;
    digital_default_params(&p); // fills the fields not read below
    p.feedback = feedback_->load();
    p.ratio = ratio_->load();
    p.detune = detune_->load();
    p.crunch = crunch_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_->load();
    p.bend_range = bend_->load();
    return p;
}

void DigitalAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    digital_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    digital_set_params(&core_, &p);
    digital_reset(&core_);
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DigitalAudioProcessor::reset()
{
    digital_reset(&core_);
}

bool DigitalAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DigitalAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    digital_set_params(&core_, &params);

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
            digital_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            digital_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            digital_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            digital_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                digital_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                digital_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                digital_all_off(&core_);
        }
    }
    if (pos < n)
        digital_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    digital_activity act;
    digital_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
}

DigitalAudioProcessor::Activity DigitalAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* DigitalAudioProcessor::createEditor()
{
    return new DigitalAudioProcessorEditor(*this);
}

void DigitalAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DigitalAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DigitalAudioProcessor();
}
