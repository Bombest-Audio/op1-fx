#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kWave{"wave", 1};
const juce::ParameterID kBlend{"blend", 1};
const juce::ParameterID kMotion{"motion", 1};
const juce::ParameterID kFold{"fold", 1};
const juce::ParameterID kTone{"tone", 1};
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

// A skewed time range with the maths in double. JUCE's own skew works in float, and a randomised state saved and
// reloaded through CLAP came back a few ulps off on Decay, which clap-validator's state tests reject.
juce::NormalisableRange<float> timeRange(float lo, float hi, float skew)
{
    return juce::NormalisableRange<float>(
        lo, hi,
        [=](float a, float b, float n) {
            const double p = n <= 0.0f ? 0.0 : std::exp(std::log(static_cast<double>(n)) / skew);
            return static_cast<float>(a + (b - a) * p);
        },
        [=](float a, float b, float v) {
            const double p = (static_cast<double>(v) - a) / (static_cast<double>(b) - a);
            return static_cast<float>(p <= 0.0 ? 0.0 : std::exp(std::log(p) * skew));
        },
        [](float a, float b, float v) { return juce::jlimit(a, b, v); });
}

juce::String seconds(float v, int)
{
    return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 2) + " s";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout DrWaveAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kWave, "Wave", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_WAVE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return juce::String(dw_wave_position(v), 2);
        }).withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue() / 7.0f; })));
    layout.add(std::make_unique<AudioParameterFloat>(kBlend, "Blend", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_BLEND,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kMotion, "Motion", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_MOTION,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kFold, "Fold", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_FOLD,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", timeRange(0.0005f, 5.0f, 0.3f),
                                                      DW_DEFAULT_ATTACK,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", timeRange(0.005f, 10.0f, 0.3f),
                                                      DW_DEFAULT_DECAY,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      DW_DEFAULT_SUSTAIN,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", timeRange(0.005f, 10.0f, 0.3f),
                                                      DW_DEFAULT_RELEASE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_LEVEL,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    layout.add(std::make_unique<AudioParameterFloat>(kTone, "Tone", NormalisableRange<float>(0.0f, 1.0f), DW_DEFAULT_TONE,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return juce::String(juce::roundToInt(dw_tone_hz(v))) + " Hz";
        }).withValueFromStringFunction([](const juce::String& t) {
            return juce::jlimit(0.0f, 1.0f, std::log(juce::jmax(t.getFloatValue(), 1.0f) / 80.0f) / std::log(250.0f));
        })));
    return layout;
}

DrWaveAudioProcessor::DrWaveAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DrWaveState", createLayout())
{
    wave_ = apvts.getRawParameterValue("wave");
    blend_ = apvts.getRawParameterValue("blend");
    motion_ = apvts.getRawParameterValue("motion");
    fold_ = apvts.getRawParameterValue("fold");
    tone_ = apvts.getRawParameterValue("tone");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    dw_init(core_.get(), 44100.0f);
}

dw_params DrWaveAudioProcessor::readParams() const
{
        dw_params p;
    dw_default_params(&p);
    p.wave = wave_->load();
    p.blend = blend_->load();
    p.motion = motion_->load();
    p.fold = fold_->load();
    p.tone = tone_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_->load();
    p.bend_range = bend_->load();
    return p;
}

void DrWaveAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dw_init(core_.get(), static_cast<float>(sampleRate));
    const auto p = readParams();
    dw_set_params(core_.get(), &p);
    dw_reset(core_.get());
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DrWaveAudioProcessor::reset()
{
    dw_reset(core_.get());
}

bool DrWaveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DrWaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    const auto params = readParams();
    dw_set_params(core_.get(), &params);

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
            dw_render(core_.get(), l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            dw_note_on(core_.get(), msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            dw_note_off(core_.get(), msg.getNoteNumber());
        else if (msg.isPitchWheel())
            dw_pitch_bend(core_.get(), static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                dw_mod_wheel(core_.get(), static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                dw_sustain_pedal(core_.get(), v >= 64);
            else if (cc == 120 || cc == 123)
                dw_all_off(core_.get());
        }
    }
    if (pos < n)
        dw_render(core_.get(), l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    dw_activity act;
    dw_take_activity(core_.get(), &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
}

DrWaveAudioProcessor::Activity DrWaveAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* DrWaveAudioProcessor::createEditor()
{
    return new DrWaveAudioProcessorEditor(*this);
}

void DrWaveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DrWaveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrWaveAudioProcessor();
}
