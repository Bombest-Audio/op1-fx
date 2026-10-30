#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PulseAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using juce::ParameterID;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"shape", 1}, "Shape", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_SHAPE));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"sweep", 1}, "Sweep", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_SWEEP));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"rate", 1}, "Rate", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_RATE));
    // Detune is shown in cents between the two oscillators (value x 100). One decimal keeps text -> value -> text stable.
    layout.add(std::make_unique<AudioParameterFloat>(
        ParameterID{"detune", 1}, "Detune", NormalisableRange<float>(-1.0f, 1.0f), PULSE_DEFAULT_DETUNE,
        juce::AudioParameterFloatAttributes()
            .withLabel("cents")
            .withStringFromValueFunction([](float v, int) { return juce::String(static_cast<double>(v) * 100.0, 1); })
            .withValueFromStringFunction([](const juce::String& t) { return t.getFloatValue() / 100.0f; })));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"attack", 1}, "Attack", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_ATTACK));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"decay", 1}, "Decay", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_DECAY));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"sustain", 1}, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_SUSTAIN));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"release", 1}, "Release", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_RELEASE));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{"level", 1}, "Level", NormalisableRange<float>(0.0f, 1.0f),
                                                      PULSE_DEFAULT_LEVEL));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID{"bend", 1}, "Bend Range", 1, 12,
                                                    static_cast<int>(PULSE_DEFAULT_BEND_RANGE)));
    return layout;
}

PulseAudioProcessor::PulseAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PulseState", createLayout())
{
    shape_ = apvts.getRawParameterValue("shape");
    sweep_ = apvts.getRawParameterValue("sweep");
    rate_ = apvts.getRawParameterValue("rate");
    detune_ = apvts.getRawParameterValue("detune");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_param_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");
    pulse_init(&core_, 44100.0f);
}

pulse_params PulseAudioProcessor::readParams() const
{
    pulse_params p;
    p.shape = shape_->load();
    p.sweep = sweep_->load();
    p.rate = rate_->load();
    p.detune = detune_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_param_->load();
    p.bend_range = bend_->load();
    return p;
}

void PulseAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    pulse_init(&core_, static_cast<float>(sampleRate));
    const auto n = static_cast<size_t>(juce::jmax(samplesPerBlock, 1));
    left_.assign(n, 0.0f);
    right_.assign(n, 0.0f);
    const auto p = readParams();
    pulse_set_params(&core_, &p);
    pulse_reset(&core_);
}

bool PulseAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PulseAudioProcessor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        pulse_note_on(&core_, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        pulse_note_off(&core_, m.getNoteNumber());
    else if (m.isPitchWheel())
        pulse_pitch_bend(&core_, static_cast<float>(m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isController())
    {
        switch (m.getControllerNumber())
        {
            case 1: pulse_mod_wheel(&core_, static_cast<float>(m.getControllerValue()) / 127.0f); break;
            case 64: pulse_sustain_pedal(&core_, m.getControllerValue() >= 64 ? 1 : 0); break;
            case 120: pulse_all_sound_off(&core_); break;
            case 123: pulse_all_off(&core_); break;
            default: break;
        }
    }
    else if (m.isAllNotesOff())
        pulse_all_off(&core_);
    else if (m.isAllSoundOff())
        pulse_all_sound_off(&core_);
}

void PulseAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (static_cast<int>(left_.size()) < n)
    {
        left_.resize(static_cast<size_t>(n));
        right_.resize(static_cast<size_t>(n));
    }

    const auto params = readParams();
    pulse_set_params(&core_, &params);

    // Render in pieces split at each MIDI event so notes start on their exact sample.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos)
        {
            pulse_render(&core_, left_.data() + pos, right_.data() + pos, at - pos);
            pos = at;
        }
        handleMidi(meta.getMessage());
    }
    if (pos < n)
        pulse_render(&core_, left_.data() + pos, right_.data() + pos, n - pos);
    midi.clear();

    if (channels >= 2)
    {
        buffer.copyFrom(0, 0, left_.data(), n);
        buffer.copyFrom(1, 0, right_.data(), n);
    }
    else if (channels == 1)
    {
        auto* out = buffer.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            out[i] = 0.5f * (left_[static_cast<size_t>(i)] + right_[static_cast<size_t>(i)]);
    }
    for (int ch = 2; ch < channels; ++ch)
        buffer.clear(ch, 0, n);

    const float peak = pulse_take_activity(&core_);
    float seen = level_.load(std::memory_order_relaxed);
    while (peak > seen && !level_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
    voices_.store(pulse_active_voices(&core_), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PulseAudioProcessor::createEditor()
{
    return new PulseAudioProcessorEditor(*this);
}

void PulseAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PulseAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PulseAudioProcessor();
}
