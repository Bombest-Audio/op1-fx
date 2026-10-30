#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum ParamIndex { kPick, kDamp, kStiff, kDecay, kAttack, kEnvDecay, kSustain, kRelease, kLevel, kBend, kCount };

const char* const kIds[kCount] = {"pick", "damp", "stiff", "decay", "attack", "env_decay", "sustain", "release", "level",
                                  "bend_range"};

float raiseTo(std::atomic<float>& a, float v) noexcept
{
    float seen = a.load(std::memory_order_relaxed);
    while (v > seen && !a.compare_exchange_weak(seen, v, std::memory_order_relaxed)) {}
    return seen;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout StringAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterFloatAttributes;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto unit = [](const char* id, const char* name, float def) {
        return std::make_unique<AudioParameterFloat>(juce::ParameterID{id, 1}, name, NormalisableRange<float>(0.0f, 1.0f), def);
    };
    layout.add(unit(kIds[kPick], "Pick", OPST_DEFAULT_PICK));
    layout.add(unit(kIds[kDamp], "Damp", OPST_DEFAULT_DAMP));
    layout.add(unit(kIds[kStiff], "Stiff", OPST_DEFAULT_STIFF));
    layout.add(unit(kIds[kDecay], "Decay", OPST_DEFAULT_DECAY));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kIds[kAttack], 1}, "Attack",
                                                      NormalisableRange<float>(0.0f, 1.0f), stringtime::toNorm(OPST_DEFAULT_ATTACK, stringtime::kAttackLo, stringtime::kAttackHi)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kIds[kEnvDecay], 1}, "Env Decay",
                                                      NormalisableRange<float>(0.0f, 1.0f), stringtime::toNorm(OPST_DEFAULT_ENV_DECAY, stringtime::kLongLo, stringtime::kLongHi)));
    layout.add(unit(kIds[kSustain], "Sustain", OPST_DEFAULT_SUSTAIN));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kIds[kRelease], 1}, "Release",
                                                      NormalisableRange<float>(0.0f, 1.0f), stringtime::toNorm(OPST_DEFAULT_RELEASE, stringtime::kLongLo, stringtime::kLongHi)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kIds[kLevel], 1}, "Level",
                                                      NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 0.0f,
                                                      AudioParameterFloatAttributes().withLabel("dB")));
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kIds[kBend], 1}, "Bend Range", 0, 24, 2));
    return layout;
}

StringAudioProcessor::StringAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "StringState", createLayout())
{
    for (int i = 0; i < kCount; ++i)
        p_[static_cast<size_t>(i)] = apvts.getRawParameterValue(kIds[i]);
    for (auto& v : voiceLevel_)
        v.store(0.0f);
    for (auto& n : voiceNote_)
        n.store(-1);
    core_ = std::make_unique<opst_t>();
    opst_init(core_.get(), 44100.0f);
}

void StringAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    opst_init(core_.get(), static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
    applyParams();
    opst_reset(core_.get());
}

bool StringAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void StringAudioProcessor::applyParams()
{
    auto v = [this](ParamIndex i) { return p_[static_cast<size_t>(i)]->load(); };
    const float db = v(kLevel);
    using namespace stringtime;
    opst_params p{v(kPick), v(kDamp), v(kStiff), v(kDecay),
                  toSeconds(v(kAttack), kAttackLo, kAttackHi), toSeconds(v(kEnvDecay), kLongLo, kLongHi), v(kSustain),
                  toSeconds(v(kRelease), kLongLo, kLongHi),
                  db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain(db)};
    opst_set_params(core_.get(), &p);
}

void StringAudioProcessor::handleMidi(const juce::MidiMessage& m)
{
    opst_t* c = core_.get();
    if (m.isNoteOn())
        opst_note_on(c, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        opst_note_off(c, m.getNoteNumber());
    else if (m.isPitchWheel())
        opst_pitch_bend(c, static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f * p_[kBend]->load());
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 1)
            opst_mod_wheel(c, static_cast<float>(val) / 127.0f);
        else if (cc == 64)
            opst_sustain(c, val >= 64);
        else if (cc == 120)
            opst_reset(c); // all sound off: silence now, no release
        else if (cc == 123)
            opst_all_off(c);
    }
    else if (m.isAllNotesOff())
        opst_all_off(c);
    else if (m.isAllSoundOff())
        opst_reset(c);
}

void StringAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1)
        return;
    if (static_cast<int>(scratch_.size()) < n)
        scratch_.resize(static_cast<size_t>(n)); // only if the host exceeds the size it announced

    applyParams();

    // Render into the output (left) and scratch (right), split at every MIDI event so notes start on their sample.
    float* l = buffer.getWritePointer(0);
    float* r = channels >= 2 ? buffer.getWritePointer(1) : scratch_.data();
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(pos, n, meta.samplePosition);
        if (at > pos)
        {
            opst_render(core_.get(), l + pos, r + pos, at - pos);
            pos = at;
        }
        handleMidi(meta.getMessage());
    }
    if (pos < n)
        opst_render(core_.get(), l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);
    for (int ch = 2; ch < channels; ++ch)
        buffer.clear(ch, 0, n);
    midi.clear();

    raiseTo(level_, opst_take_level(core_.get()));
    for (int i = 0; i < OPST_VOICES; ++i)
    {
        int note = 0;
        float lvl = 0.0f;
        const bool on = opst_voice_info(core_.get(), i, &note, &lvl) != 0;
        auto idx = static_cast<size_t>(i);
        raiseTo(voiceLevel_[idx], on ? lvl : 0.0f);
        voiceNote_[idx].store(on ? note : -1, std::memory_order_relaxed);
    }
}

StringAudioProcessor::Activity StringAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.level = level_.exchange(0.0f);
    for (size_t i = 0; i < OPST_VOICES; ++i)
    {
        a.voice[i] = voiceLevel_[i].exchange(0.0f);
        a.voiceNote[i] = voiceNote_[i].load(std::memory_order_relaxed);
    }
    return a;
}

juce::AudioProcessorEditor* StringAudioProcessor::createEditor()
{
    return new StringAudioProcessorEditor(*this);
}

void StringAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void StringAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StringAudioProcessor();
}
