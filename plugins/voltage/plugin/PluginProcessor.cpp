#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout VoltageAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const char* id, const char* name, NormalisableRange<float> r, float def) {
        layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{id, 1}, name, r, def));
    };
    add("xor", "XOR", {0.0f, 1.0f}, OPVL_DEFAULT_XOR);
    add("feedback", "Feedback", {0.0f, 1.0f}, OPVL_DEFAULT_FEEDBACK);
    add("filter", "Filter", {0.0f, 1.0f}, OPVL_DEFAULT_FILTER);
    add("detune", "Detune", {0.0f, 1.0f}, OPVL_DEFAULT_DETUNE);
    add("attack", "Attack", {0.0f, 1.0f}, voltagetime::toNorm(OPVL_DEFAULT_ATTACK, voltagetime::kAttackLo, voltagetime::kAttackHi));
    add("decay", "Decay", {0.0f, 1.0f}, voltagetime::toNorm(OPVL_DEFAULT_ENV_DECAY, voltagetime::kLongLo, voltagetime::kLongHi));
    add("sustain", "Sustain", {0.0f, 1.0f}, OPVL_DEFAULT_SUSTAIN);
    add("release", "Release", {0.0f, 1.0f}, voltagetime::toNorm(OPVL_DEFAULT_RELEASE, voltagetime::kLongLo, voltagetime::kLongHi));
    add("level", "Level", {0.0f, 2.0f}, OPVL_DEFAULT_LEVEL);
    return layout;
}

VoltageAudioProcessor::VoltageAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "VoltageState", createLayout())
{
    xor_ = apvts.getRawParameterValue("xor");
    fb_ = apvts.getRawParameterValue("feedback");
    filter_ = apvts.getRawParameterValue("filter");
    detune_ = apvts.getRawParameterValue("detune");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_p_ = apvts.getRawParameterValue("level");
    opvl_init(&core_, 44100.0f);
}

opvl_params VoltageAudioProcessor::readParams() const
{
    opvl_params p;
    p.xor_amt = xor_->load();
    p.feedback = fb_->load();
    p.filter = filter_->load();
    p.detune = detune_->load();
    p.env_attack = voltagetime::toSeconds(attack_->load(), voltagetime::kAttackLo, voltagetime::kAttackHi);
    p.env_decay = voltagetime::toSeconds(decay_->load(), voltagetime::kLongLo, voltagetime::kLongHi);
    p.env_sustain = sustain_->load();
    p.env_release = voltagetime::toSeconds(release_->load(), voltagetime::kLongLo, voltagetime::kLongHi);
    p.level = level_p_->load();
    return p;
}

void VoltageAudioProcessor::prepareToPlay(double sampleRate, int)
{
    opvl_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    opvl_set_params(&core_, &p);
    opvl_reset(&core_);
}

bool VoltageAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VoltageAudioProcessor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        opvl_note_on(&core_, m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        opvl_note_off(&core_, m.getNoteNumber());
    else if (m.isPitchWheel())
        opvl_pitch_bend(&core_, (static_cast<float>(m.getPitchWheelValue()) - 8192.0f) / 8192.0f * bendRange_);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 1)
            opvl_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
        else if (cc == 64)
            opvl_sustain(&core_, v >= 64);
        else if (cc == 123 || cc == 120)
            opvl_all_off(&core_);
    }
}

void VoltageAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    const auto p = readParams();
    opvl_set_params(&core_, &p);

    float* l = buffer.getWritePointer(0);
    float* r = channels > 1 ? buffer.getWritePointer(1) : nullptr;
    float scratch[8192];
    int pos = 0;
    auto renderTo = [&](int end) {
        while (pos < end)
        {
            const int m = juce::jmin(end - pos, 4096);
            if (r != nullptr)
                opvl_render(&core_, l + pos, r + pos, m);
            else
            {
                opvl_render(&core_, l + pos, scratch, m);
            }
            pos += m;
        }
    };
    for (const auto meta : midi)
    {
        renderTo(juce::jlimit(0, n, meta.samplePosition));
        handleMidi(meta.getMessage());
    }
    renderTo(n);

    const float peak = opvl_take_level(&core_);
    float seen = level_.load(std::memory_order_relaxed);
    while (peak > seen && !level_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {}
    voices_.store(opvl_active_voices(&core_));
}

juce::AudioProcessorEditor* VoltageAudioProcessor::createEditor()
{
    return new VoltageAudioProcessorEditor(*this);
}

void VoltageAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void VoltageAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoltageAudioProcessor();
}
