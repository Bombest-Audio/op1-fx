#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::ParameterID kStart{"start", 1};
const juce::ParameterID kEnd{"end", 1};
const juce::ParameterID kTune{"tune", 1};
const juce::ParameterID kLoop{"loop", 1};
const juce::ParameterID kAttack{"attack", 1};
const juce::ParameterID kDecay{"decay", 1};
const juce::ParameterID kSustain{"sustain", 1};
const juce::ParameterID kRelease{"release", 1};
const juce::ParameterID kLevel{"level", 1};
const juce::ParameterID kRoot{"root", 1};
const juce::ParameterID kBend{"bend", 1};

constexpr int kMaxSampleSeconds = 30;
const juce::Identifier kPathProp("samplePath");

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

juce::String semitones(float v, int)
{
    return (v >= 0.0f ? "+" : "") + juce::String(v, 1);
}

juce::String loopText(float v, int)
{
    return v <= 0.0f ? juce::String("OFF") : juce::String(juce::roundToInt(v * 100.0f));
}

float fromLoop(const juce::String& t)
{
    return t.trim().equalsIgnoreCase("off") ? 0.0f : t.getFloatValue() / 100.0f;
}

std::array<float, 128> computePeaks(const std::vector<float>& d)
{
    std::array<float, 128> p{};
    if (d.empty())
        return p;
    for (size_t b = 0; b < p.size(); ++b)
    {
        const size_t from = b * d.size() / p.size();
        const size_t to = juce::jmax(from + 1, (b + 1) * d.size() / p.size());
        float m = 0.0f;
        for (size_t i = from; i < to && i < d.size(); ++i)
            m = juce::jmax(m, std::abs(d[i]));
        p[b] = juce::jmin(m, 1.0f);
    }
    return p;
}
} // namespace

juce::String samplerNoteName(int note)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    note = juce::jlimit(0, 127, note);
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

int noteFromText(const juce::String& t)
{
    // "C4", "F#3", "A-1", or a bare number
    const auto text = t.trim();
    if (text.isEmpty())
        return 60;
    if (text.containsOnly("0123456789"))
        return juce::jlimit(0, 127, text.getIntValue());
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int best = -1, bestLen = 0;
    for (int i = 0; i < 12; ++i)
    {
        const juce::String n(names[i]);
        if (text.startsWithIgnoreCase(n) && n.length() > bestLen)
        {
            best = i;
            bestLen = n.length();
        }
    }
    if (best < 0)
        return 60;
    return juce::jlimit(0, 127, (text.substring(bestLen).getIntValue() + 1) * 12 + best);
}

juce::AudioProcessorValueTreeState::ParameterLayout SamplerAudioProcessor::createLayout()
{
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;
    using juce::NormalisableRange;
    using Attr = juce::AudioParameterFloatAttributes;

    // Parameter order and IDs are part of saved sessions. Append only, never reorder.
    // The time controls snap to 0.5 ms: a skewed range stored as a float does not otherwise round-trip exactly through
    // saved state, which clap-validator catches.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(kStart, "Start", NormalisableRange<float>(0.0f, 1.0f), SAMPLER_DEFAULT_START,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kEnd, "End", NormalisableRange<float>(0.0f, 1.0f), SAMPLER_DEFAULT_END,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kTune, "Tune", NormalisableRange<float>(-SAMPLER_TUNE_RANGE, SAMPLER_TUNE_RANGE),
                                                      SAMPLER_DEFAULT_TUNE,
        Attr().withStringFromValueFunction(semitones)));
    layout.add(std::make_unique<AudioParameterFloat>(kLoop, "Loop", NormalisableRange<float>(0.0f, 1.0f), SAMPLER_DEFAULT_LOOP,
        Attr().withStringFromValueFunction(loopText).withValueFromStringFunction(fromLoop)));
    layout.add(std::make_unique<AudioParameterFloat>(kAttack, "Attack", NormalisableRange<float>(0.0005f, 5.0f, 0.0005f, 0.3f),
                                                      SAMPLER_DEFAULT_ATTACK,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kDecay, "Decay", NormalisableRange<float>(0.005f, 10.0f, 0.0005f, 0.3f),
                                                      SAMPLER_DEFAULT_DECAY,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kSustain, "Sustain", NormalisableRange<float>(0.0f, 1.0f),
                                                      SAMPLER_DEFAULT_SUSTAIN,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterFloat>(kRelease, "Release", NormalisableRange<float>(0.005f, 10.0f, 0.0005f, 0.3f),
                                                      SAMPLER_DEFAULT_RELEASE,
        Attr().withStringFromValueFunction(seconds).withValueFromStringFunction(fromSeconds)));
    layout.add(std::make_unique<AudioParameterFloat>(kLevel, "Level", NormalisableRange<float>(0.0f, 1.0f), SAMPLER_DEFAULT_LEVEL,
        Attr().withStringFromValueFunction(percent).withValueFromStringFunction(fromPercent)));
    layout.add(std::make_unique<AudioParameterInt>(kRoot, "Root Note", 0, 127, 60,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int v, int) { return samplerNoteName(v); })
            .withValueFromStringFunction(noteFromText)));
    layout.add(std::make_unique<AudioParameterInt>(kBend, "Bend Range", 1, 12, 2));
    return layout;
}

SamplerAudioProcessor::SamplerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SamplerState", createLayout())
{
    start_ = apvts.getRawParameterValue("start");
    end_ = apvts.getRawParameterValue("end");
    tune_ = apvts.getRawParameterValue("tune");
    loop_ = apvts.getRawParameterValue("loop");
    attack_ = apvts.getRawParameterValue("attack");
    decay_ = apvts.getRawParameterValue("decay");
    sustain_ = apvts.getRawParameterValue("sustain");
    release_ = apvts.getRawParameterValue("release");
    level_ = apvts.getRawParameterValue("level");
    root_ = apvts.getRawParameterValue("root");
    bend_ = apvts.getRawParameterValue("bend");
    sampler_init(&core_, 44100.0f);
    loadDemoSample(0, false);
}

sampler_params SamplerAudioProcessor::readParams() const
{
    sampler_params p;
    sampler_default_params(&p);
    p.start = start_->load();
    p.end = end_->load();
    p.tune = tune_->load();
    p.loop = loop_->load();
    p.attack = attack_->load();
    p.decay = decay_->load();
    p.sustain = sustain_->load();
    p.release = release_->load();
    p.level = level_->load();
    p.root = root_->load();
    p.bend_range = bend_->load();
    return p;
}

void SamplerAudioProcessor::publish(std::unique_ptr<SamplerData> data, const SampleInfo& info)
{
    const juce::ScopedLock sl(ownLock_);
    const SamplerData* raw = data.get();
    owned_.push_back(std::move(data));
    info_ = info;
    latest_.store(raw, std::memory_order_release);
    // Keep the three newest loads and whatever the audio thread is using; free the rest.
    const SamplerData* using_ = inUse_.load(std::memory_order_acquire);
    for (size_t i = 0; i + 3 < owned_.size();)
    {
        if (owned_[i].get() == using_)
            ++i;
        else
            owned_.erase(owned_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

bool SamplerAudioProcessor::loadSampleFile(const juce::File& file, bool setRoot)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0 || reader->numChannels < 1)
        return false;

    const auto frames = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples,
                                                                  static_cast<juce::int64>(reader->sampleRate * kMaxSampleSeconds)));
    juce::AudioBuffer<float> buf(static_cast<int>(juce::jmin<unsigned>(reader->numChannels, 2u)), frames);
    if (!reader->read(&buf, 0, frames, 0, true, true))
        return false;

    auto data = std::make_unique<SamplerData>();
    data->rate = static_cast<float>(reader->sampleRate);
    data->data.resize(static_cast<size_t>(frames));
    const int ch = buf.getNumChannels();
    for (int i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < ch; ++c)
            sum += buf.getSample(c, i);
        data->data[static_cast<size_t>(i)] = sum / static_cast<float>(ch);
    }

    SampleInfo info;
    info.name = file.getFileNameWithoutExtension();
    info.path = file.getFullPathName();
    info.peaks = computePeaks(data->data);
    apvts.state.setProperty(kPathProp, info.path, nullptr);
    publish(std::move(data), info);
    if (setRoot)
    {
        const int root = juce::jlimit(0, 127, reader->metadataValues.getValue("MidiUnityNote", "60").getIntValue());
        if (auto* rp = apvts.getParameter("root"))
            rp->setValueNotifyingHost(rp->convertTo0to1(static_cast<float>(root)));
    }
    return true;
}

void SamplerAudioProcessor::loadDemoSample(int kind, bool setRoot)
{
    kind = juce::jlimit(0, SAMPLER_DEMO_KINDS - 1, kind);
    auto data = std::make_unique<SamplerData>();
    data->rate = 44100.0f;
    data->data.resize(static_cast<size_t>(sampler_demo_frames(kind, data->rate)));
    sampler_demo_render(kind, data->rate, data->data.data(), static_cast<int>(data->data.size()));

    SampleInfo info;
    info.name = juce::String(sampler_demo_name(kind)) + " (built in)";
    info.peaks = computePeaks(data->data);
    apvts.state.removeProperty(kPathProp, nullptr);
    publish(std::move(data), info);
    if (setRoot)
        if (auto* rp = apvts.getParameter("root"))
            rp->setValueNotifyingHost(rp->convertTo0to1(static_cast<float>(sampler_demo_root(kind))));
}

SamplerAudioProcessor::SampleInfo SamplerAudioProcessor::getSampleInfo() const
{
    const juce::ScopedLock sl(ownLock_);
    return info_;
}

void SamplerAudioProcessor::restoreSampleFromState()
{
    const auto path = apvts.state.getProperty(kPathProp).toString();
    if (path.isEmpty())
    {
        loadDemoSample(0, false);
        return;
    }
    const juce::File f(path);
    if (f.existsAsFile() && loadSampleFile(f, false))
        return;
    // The file moved or went away: play the built-in sample, keep the saved path so a later save does not lose it,
    // and tell the editor.
    loadDemoSample(0, false);
    apvts.state.setProperty(kPathProp, path, nullptr);
    const juce::ScopedLock sl(ownLock_);
    info_.missing = true;
    info_.path = path;
    info_.name = f.getFileNameWithoutExtension();
}

void SamplerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampler_init(&core_, static_cast<float>(sampleRate));
    const auto p = readParams();
    sampler_set_params(&core_, &p);
    current_ = nullptr; // sampler_init dropped the sample; the next block hands it over again
    scratch_.assign(static_cast<size_t>(juce::jmax(samplesPerBlock, 1)), 0.0f);
}

void SamplerAudioProcessor::reset()
{
    sampler_reset(&core_);
}

bool SamplerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SamplerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (channels < 1 || n < 1)
        return;

    if (const SamplerData* latest = latest_.load(std::memory_order_acquire); latest != current_ && latest != nullptr)
    {
        sampler_sample v;
        v.data = latest->data.data();
        v.frames = static_cast<int>(latest->data.size());
        v.rate = latest->rate;
        sampler_set_sample(&core_, &v);
        current_ = latest;
        inUse_.store(latest, std::memory_order_release);
    }

    const auto params = readParams();
    sampler_set_params(&core_, &params);

    if (static_cast<int>(scratch_.size()) < n)
        scratch_.resize(static_cast<size_t>(n)); // only if a host exceeds the size it announced

    float* l = buffer.getWritePointer(0);
    float* r = channels >= 2 ? buffer.getWritePointer(1) : scratch_.data();

    // Sample-accurate notes: render up to each event, apply it, carry on. The core output does not depend on how the
    // block is split.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit(pos, n, meta.samplePosition);
        if (at > pos)
        {
            sampler_render(&core_, l + pos, r + pos, at - pos);
            pos = at;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            sampler_note_on(&core_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            sampler_note_off(&core_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            sampler_pitch_bend(&core_, static_cast<float>(msg.getPitchWheelValue() - 8192) / 8192.0f);
        else if (msg.isController())
        {
            const int cc = msg.getControllerNumber();
            const int v = msg.getControllerValue();
            if (cc == 1)
                sampler_mod_wheel(&core_, static_cast<float>(v) / 127.0f);
            else if (cc == 64)
                sampler_sustain_pedal(&core_, v >= 64);
            else if (cc == 120 || cc == 123)
                sampler_all_off(&core_);
        }
    }
    if (pos < n)
        sampler_render(&core_, l + pos, r + pos, n - pos);

    if (channels == 1)
        for (int i = 0; i < n; ++i)
            l[i] = 0.5f * (l[i] + r[i]);

    sampler_activity act;
    sampler_take_activity(&core_, &act);
    float seen = peak_.load(std::memory_order_relaxed);
    while (act.peak > seen && !peak_.compare_exchange_weak(seen, act.peak, std::memory_order_relaxed)) {}
    voices_.store(act.voices, std::memory_order_relaxed);
    notes_.store(act.notes, std::memory_order_relaxed);
    head_.store(act.head, std::memory_order_relaxed);
}

SamplerAudioProcessor::Activity SamplerAudioProcessor::takeActivity() noexcept
{
    Activity a;
    a.peak = peak_.exchange(0.0f);
    a.voices = voices_.load(std::memory_order_relaxed);
    a.notes = notes_.load(std::memory_order_relaxed);
    a.head = head_.load(std::memory_order_relaxed);
    return a;
}

juce::AudioProcessorEditor* SamplerAudioProcessor::createEditor()
{
    return new SamplerAudioProcessorEditor(*this);
}

void SamplerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void SamplerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            restoreSampleFromState();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SamplerAudioProcessor();
}
