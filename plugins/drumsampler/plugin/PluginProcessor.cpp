#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr int kMaxFileSeconds = 60;

// JUCE's bool parameter can retain a fractional host value even though APVTS
// restores its snapped value. Report the same 0 or 1 before and after restore.
class SnappedBoolParameter final : public juce::AudioParameterBool
{
public:
    using juce::AudioParameterBool::AudioParameterBool;

    float getValue() const override { return get() ? 1.0f : 0.0f; }
};

juce::String percent(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; }
float fromPercent(const juce::String& t) { return t.getFloatValue() / 100.0f; }

juce::String semitones(float v, int)
{
    return (v > 0.004f ? "+" : "") + juce::String(v, 2) + " st";
}
float fromSemitones(const juce::String& t) { return t.getFloatValue(); }

juce::String decibels(float v, int)
{
    return v < 0.0001f ? juce::String("-inf dB") : juce::String(20.0f * std::log10(v), 1) + " dB";
}
float fromDecibels(const juce::String& t)
{
    const auto text = t.trim().toLowerCase();
    if (text.startsWith("-inf"))
        return 0.0f;
    return std::pow(10.0f, text.getFloatValue() / 20.0f);
}

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
} // namespace

juce::String DrumSamplerAudioProcessor::slotParamId(int slot, const char* what)
{
    return juce::String::formatted("s%02d_", slot + 1) + what;
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumSamplerAudioProcessor::createLayout()
{
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using Attr = juce::AudioParameterFloatAttributes;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int s = 0; s < DS_SLOTS; ++s)
    {
        const auto name = juce::String::formatted("Slot %02d ", s + 1);
        layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{slotParamId(s, "pitch"), 1}, name + "Pitch",
            NormalisableRange<float>(-24.0f, 24.0f), DS_DEFAULT_PITCH,
            Attr().withStringFromValueFunction(semitones).withValueFromStringFunction(fromSemitones)));
        layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{slotParamId(s, "in"), 1}, name + "In",
            NormalisableRange<float>(0.0f, 1.0f), DS_DEFAULT_IN,
            Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
        layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{slotParamId(s, "out"), 1}, name + "Out",
            NormalisableRange<float>(0.0f, 1.0f), DS_DEFAULT_OUT,
            Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
        layout.add(std::make_unique<AudioParameterChoice>(juce::ParameterID{slotParamId(s, "mode"), 1}, name + "Mode",
            juce::StringArray{"Gate", "One-shot", "Loop"}, DS_DEFAULT_MODE));
        layout.add(std::make_unique<SnappedBoolParameter>(juce::ParameterID{slotParamId(s, "reverse"), 1}, name + "Reverse", false));
        layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{slotParamId(s, "gain"), 1}, name + "Level",
            NormalisableRange<float>(0.0f, 2.0f), DS_DEFAULT_GAIN,
            Attr().withStringFromValueFunction(decibels).withValueFromStringFunction(fromDecibels)));
    }

    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"attack", 1}, "Attack",
        NormalisableRange<float>(0.0005f, 5.0f, 0.0f, 0.3f), DS_DEFAULT_ATTACK,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"decay", 1}, "Decay",
        NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f), DS_DEFAULT_DECAY,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"sustain", 1}, "Sustain",
        NormalisableRange<float>(0.0f, 1.0f), DS_DEFAULT_SUSTAIN,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"release", 1}, "Release",
        NormalisableRange<float>(0.005f, 10.0f, 0.0f, 0.3f), DS_DEFAULT_RELEASE,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{"level", 1}, "Output Level",
        NormalisableRange<float>(0.0f, 1.0f), DS_DEFAULT_LEVEL,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{"bend", 1}, "Bend Range", 1, 12, 2));
    return layout;
}

DrumSamplerAudioProcessor::DrumSamplerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DrumSamplerState", createLayout())
{
    for (int s = 0; s < DS_SLOTS; ++s)
    {
        slotParams_[static_cast<size_t>(s)] = {apvts.getRawParameterValue(slotParamId(s, "pitch")),
                                               apvts.getRawParameterValue(slotParamId(s, "in")),
                                               apvts.getRawParameterValue(slotParamId(s, "out")),
                                               apvts.getRawParameterValue(slotParamId(s, "mode")),
                                               apvts.getRawParameterValue(slotParamId(s, "reverse")),
                                               apvts.getRawParameterValue(slotParamId(s, "gain"))};
        pending_[static_cast<size_t>(s)].store(nullptr);
        applied_[static_cast<size_t>(s)].store(nullptr);
        head_[static_cast<size_t>(s)].store(-1.0f);
    }
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    bend_ = apvts.getRawParameterValue("bend");

    formats_.registerBasicFormats();
    ds_init(&core_, 44100.0f);
    for (int s = 0; s < DS_SLOTS; ++s)
        putSample(s, makeDefault(s));
}

DrumSamplerAudioProcessor::~DrumSamplerAudioProcessor() = default;

std::shared_ptr<DrumSample> DrumSamplerAudioProcessor::makeDefault(int slot) const
{
    auto d = std::make_shared<DrumSample>();
    d->left.resize(static_cast<size_t>(ds_kit_frames(slot)));
    const int n = ds_kit_render(slot, d->left.data(), static_cast<int>(d->left.size()));
    d->left.resize(static_cast<size_t>(n));
    d->rate = DS_KIT_RATE;
    d->name = ds_kit_name(slot);
    return d;
}

void DrumSamplerAudioProcessor::putSample(int slot, std::shared_ptr<DrumSample> s)
{
    const auto i = static_cast<size_t>(slot);
    slots_[i] = s;
    library_.push_back(s);
    pending_[i].store(s.get(), std::memory_order_release);
    revision_.fetch_add(1);
    collectGarbage();
}

void DrumSamplerAudioProcessor::collectGarbage()
{
    // Keep what any slot is waiting to receive or has just been given; everything else is unreachable from the audio thread.
    library_.erase(std::remove_if(library_.begin(), library_.end(),
                                  [this](const std::shared_ptr<DrumSample>& s) {
                                      for (size_t i = 0; i < DS_SLOTS; ++i)
                                          if (pending_[i].load() == s.get() || applied_[i].load() == s.get())
                                              return false;
                                      return true;
                                  }),
                   library_.end());
}

std::shared_ptr<const DrumSample> DrumSamplerAudioProcessor::sampleFor(int slot) const
{
    return slot >= 0 && slot < DS_SLOTS ? slots_[static_cast<size_t>(slot)] : nullptr;
}

void DrumSamplerAudioProcessor::setSelectedSlot(int slot)
{
    slot = juce::jlimit(0, DS_SLOTS - 1, slot);
    selected_.store(slot);
    apvts.state.setProperty("selected", slot, nullptr);
}

void DrumSamplerAudioProcessor::recordPath(int slot, const juce::String& path)
{
    auto samples = apvts.state.getOrCreateChildWithName("Samples", nullptr);
    auto child = samples.getChildWithProperty("index", slot);
    if (path.isEmpty())
    {
        if (child.isValid())
            samples.removeChild(child, nullptr);
        return;
    }
    if (!child.isValid())
    {
        child = juce::ValueTree("Slot");
        child.setProperty("index", slot, nullptr);
        samples.appendChild(child, nullptr);
    }
    child.setProperty("path", path, nullptr);
}

juce::String DrumSamplerAudioProcessor::loadFile(int slot, const juce::File& file)
{
    if (slot < 0 || slot >= DS_SLOTS)
        return "No such slot";
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr)
        return "Could not read " + file.getFileName();
    if (reader->lengthInSamples <= 0 || reader->numChannels < 1)
        return file.getFileName() + " is empty";
    if (reader->sampleRate < 1000.0 || reader->sampleRate > 768000.0)
        return file.getFileName() + " has an unusable sample rate";
    if (reader->lengthInSamples > static_cast<juce::int64>(reader->sampleRate * kMaxFileSeconds))
        return file.getFileName() + " is longer than " + juce::String(kMaxFileSeconds) + " seconds";

    const int frames = static_cast<int>(reader->lengthInSamples);
    const int channels = juce::jmin(2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> buf(channels, frames);
    if (!reader->read(&buf, 0, frames, 0, true, true))
        return "Could not read " + file.getFileName();

    auto d = std::make_shared<DrumSample>();
    d->left.assign(buf.getReadPointer(0), buf.getReadPointer(0) + frames);
    if (channels > 1)
        d->right.assign(buf.getReadPointer(1), buf.getReadPointer(1) + frames);
    d->rate = reader->sampleRate;
    d->name = file.getFileNameWithoutExtension();
    d->path = file.getFullPathName();
    recordPath(slot, d->path);
    putSample(slot, std::move(d));
    return {};
}

void DrumSamplerAudioProcessor::resetSlot(int slot)
{
    if (slot < 0 || slot >= DS_SLOTS)
        return;
    recordPath(slot, {});
    putSample(slot, makeDefault(slot));
}

void DrumSamplerAudioProcessor::loadSamplesFromState()
{
    std::array<juce::String, DS_SLOTS> paths;
    auto samples = apvts.state.getChildWithName("Samples");
    for (int i = 0; i < samples.getNumChildren(); ++i)
    {
        auto child = samples.getChild(i);
        const int idx = child.getProperty("index", -1);
        if (idx >= 0 && idx < DS_SLOTS)
            paths[static_cast<size_t>(idx)] = child.getProperty("path").toString();
    }
    for (int s = 0; s < DS_SLOTS; ++s)
    {
        const auto& p = paths[static_cast<size_t>(s)];
        if (p.isEmpty())
        {
            putSample(s, makeDefault(s));
            continue;
        }
        const auto err = loadFile(s, juce::File(p));
        if (err.isNotEmpty())
        {
            // Keep the saved path in the state, so fixing the file and reloading the session finds it again.
            auto d = makeDefault(s);
            d->missingPath = p;
            putSample(s, std::move(d));
            recordPath(s, p);
        }
    }
}

void DrumSamplerAudioProcessor::applyPendingSamples() noexcept
{
    for (size_t s = 0; s < DS_SLOTS; ++s)
    {
        const DrumSample* p = pending_[s].load(std::memory_order_acquire);
        if (p == nullptr || p == applied_[s].load(std::memory_order_relaxed))
            continue;
        ds_set_sample(&core_, static_cast<int>(s), p->left.data(), p->right.empty() ? nullptr : p->right.data(), p->frames(),
                      static_cast<float>(p->rate));
        applied_[s].store(p, std::memory_order_release);
    }
}

void DrumSamplerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    ds_set_sample_rate(&core_, static_cast<float>(sampleRate));
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void DrumSamplerAudioProcessor::reset()
{
    ds_reset(&core_);
}

bool DrumSamplerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DrumSamplerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    applyPendingSamples();

    ds_params gp;
    ds_default_params(&gp);
    gp.attack = attack_->load();
    gp.decay = decay_->load();
    gp.sustain = sustain_->load();
    gp.release = release_->load();
    gp.level = level_->load();
    gp.bend_range = bend_->load();
    ds_set_params(&core_, &gp);
    for (int s = 0; s < DS_SLOTS; ++s)
    {
        const auto& sp = slotParams_[static_cast<size_t>(s)];
        ds_slot_params q;
        q.pitch = sp.pitch->load();
        q.in = sp.in->load();
        q.out = sp.out->load();
        q.mode = juce::roundToInt(sp.mode->load());
        q.reverse = sp.reverse->load() >= 0.5f;
        q.gain = sp.gain->load();
        ds_set_slot_params(&core_, s, &q);
    }

    // Pads clicked in the editor arrive here as extra MIDI events at the start of the block.
    keyboardState.processNextMidiBuffer(midi, 0, n, true);

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
            ds_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            ds_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            ds_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            ds_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 64)
                ds_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                ds_all_off(&core_);
        }
    }
    if (pos < n)
        ds_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    ds_activity act;
    ds_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
    hits_.fetch_or(act.hits, std::memory_order_relaxed);
    playing_.store(act.playing, std::memory_order_relaxed);
    if (act.last_slot >= 0)
        lastSlot_.store(act.last_slot, std::memory_order_relaxed);
    for (size_t s = 0; s < DS_SLOTS; ++s)
        head_[s].store(act.head[s], std::memory_order_relaxed);
}

DrumSamplerAudioProcessor::Activity DrumSamplerAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    a.hits = hits_.exchange(0u);
    a.playing = playing_.load(std::memory_order_relaxed);
    a.lastSlot = lastSlot_.load(std::memory_order_relaxed);
    for (size_t s = 0; s < DS_SLOTS; ++s)
        a.head[s] = head_[s].load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* DrumSamplerAudioProcessor::createEditor()
{
    return new DrumSamplerAudioProcessorEditor(*this);
}

void DrumSamplerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    apvts.state.setProperty("selected", selected_.load(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DrumSamplerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            selected_.store(juce::jlimit(0, DS_SLOTS - 1, static_cast<int>(apvts.state.getProperty("selected", 0))));
            loadSamplesFromState();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrumSamplerAudioProcessor();
}
