#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kBigW = 240.0f, kBigH = 250.0f;
constexpr float kCompactW = 98.0f, kCompactH = 98.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff2ef0a0);
const juce::Colour kWhite(0xffe9ece8);
const juce::Colour kOrange(0xffff9a3d);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);
const juce::Colour kPanel(0xff0b0c10);

// Layout in the 1280x630 design space.
constexpr float kWaveX = 345.0f, kWaveY = 92.0f;
constexpr float kKnobRowY = 252.0f;
constexpr float kPadX = 345.0f, kPadY = 358.0f;
constexpr float kRevX = 353.0f, kRevY = 210.0f, kRevW = 58.0f, kRevH = 24.0f;

// Centre of the record in faceplate-bg-1280x630.png, for the glow behind the panels.
constexpr float kArtX = 640.0f, kArtY = 330.0f, kArtR = 300.0f;
constexpr int kTimerHz = 60;

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::Font mono(float height)
{
    return juce::Font(juce::FontOptions(height)).withExtraKerningFactor(0.12f);
}

juce::String pct(double v) { return juce::String(juce::roundToInt(v * 100.0)); }

juce::String secs(double v)
{
    return v < 1.0 ? juce::String(juce::roundToInt(v * 1000.0)) + " ms" : juce::String(v, 2) + " s";
}

juce::String semis(double v)
{
    const double r = std::round(v * 100.0) / 100.0;
    return (r > 0.0 ? "+" : "") + juce::String(r, 2);
}

juce::String dB(double v)
{
    return v < 0.0001 ? juce::String("-inf") : juce::String(20.0 * std::log10(v), 1) + " dB";
}

juce::String modeName(double v)
{
    switch (juce::roundToInt(v))
    {
        case 0: return "GATE";
        case 1: return "ONE-SHOT";
        default: return "LOOP";
    }
}
} // namespace

// ---------------------------------------------------------------- knob

DsKnob::DsKnob(juce::String label, juce::String caption, juce::Colour tone, Size size, std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), size_(size), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DsKnob::paint(juce::Graphics& g)
{
    const bool big = size_ == Size::Big;
    const float dw = big ? kBigW : kCompactW, dh = big ? kBigH : kCompactH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / dw, static_cast<float>(getHeight()) / dh));

    const float cx = dw * 0.5f;
    const float cy = big ? 130.0f : 44.0f;
    const float r = big ? 74.0f : 20.0f;
    const float stroke = big ? 6.2f : 3.6f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    g.setColour(big ? tone_ : kMuted);
    g.setFont(condensed(big ? 36.0f : 15.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(dw), big ? 36 : 16, juce::Justification::centred);

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float dot = big ? 7.0f : 3.4f;
    g.setColour(tone_);
    g.fillEllipse(cx + std::sin(aV) * r - dot, cy - std::cos(aV) * r - dot, dot * 2.0f, dot * 2.0f);

    g.setColour(kInk);
    const auto text = format_(getValue());
    if (!big)
    {
        g.setFont(condensed(19.0f, false));
        g.drawText(text, 0, 70, static_cast<int>(dw), 24, juce::Justification::centred);
    }
    else
    {
        g.setFont(condensed(text.length() > 5 ? 50.0f : 76.0f, false));
        g.drawText(text, 0, 92, static_cast<int>(dw), 76, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(mono(12.0f).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(dw), 18, juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- reverse toggle

DsToggle::DsToggle(juce::String text) : juce::Button(text), text_(std::move(text))
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void DsToggle::paintButton(juce::Graphics& g, bool over, bool)
{
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? kGreen.withAlpha(0.22f) : kPanel.withAlpha(0.9f));
    g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
    g.setColour(on ? kGreen : (over ? kMuted : kTrack));
    g.drawRoundedRectangle(b, b.getHeight() * 0.5f, 1.2f);
    g.setColour(on ? kGreen : kMuted);
    g.setFont(condensed(getHeight() * 0.62f, true).withExtraKerningFactor(0.2f));
    g.drawText(text_, getLocalBounds(), juce::Justification::centred);
}

// ---------------------------------------------------------------- waveform

void DsWaveform::setSample(std::shared_ptr<const DrumSample> s, int slot)
{
    sample_ = std::move(s);
    slot_ = slot;
    const int cols = static_cast<int>(kW);
    lo_.assign(static_cast<size_t>(cols), 0.0f);
    hi_.assign(static_cast<size_t>(cols), 0.0f);
    if (sample_ != nullptr && sample_->frames() > 0)
    {
        const auto n = static_cast<size_t>(sample_->frames());
        const bool st = !sample_->right.empty();
        for (int c = 0; c < cols; ++c)
        {
            const size_t a = n * static_cast<size_t>(c) / static_cast<size_t>(cols);
            size_t b = n * static_cast<size_t>(c + 1) / static_cast<size_t>(cols);
            if (b <= a)
                b = a + 1;
            float lo = 0.0f, hi = 0.0f;
            for (size_t i = a; i < b && i < n; ++i)
            {
                const float v = st ? 0.5f * (sample_->left[i] + sample_->right[i]) : sample_->left[i];
                lo = juce::jmin(lo, v);
                hi = juce::jmax(hi, v);
            }
            lo_[static_cast<size_t>(c)] = lo;
            hi_[static_cast<size_t>(c)] = hi;
        }
    }
    repaint();
}

juce::Point<float> DsWaveform::toDesign(juce::Point<float> p) const
{
    return {p.x * kW / static_cast<float>(getWidth()), p.y * kH / static_cast<float>(getHeight())};
}

void DsWaveform::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kW, static_cast<float>(getHeight()) / kH));
    const juce::Rectangle<float> all(0.0f, 0.0f, kW, kH);
    g.setColour(kPanel.withAlpha(0.86f));
    g.fillRoundedRectangle(all, 6.0f);
    g.setColour(kTrack);
    g.drawRoundedRectangle(all.reduced(0.5f), 6.0f, 1.0f);

    const float top = 40.0f, bottom = kH - 12.0f, mid = 0.5f * (top + bottom), half = 0.5f * (bottom - top);
    g.setColour(kTrack);
    g.drawHorizontalLine(static_cast<int>(mid), 10.0f, kW - 10.0f);

    const float x0 = 10.0f, x1 = kW - 10.0f, w = x1 - x0;
    const float rin = juce::jmin(in_, out_), rout = juce::jmax(in_, out_);
    const float xa = x0 + w * rin, xb = x0 + w * rout;

    if (!lo_.empty())
    {
        const int cols = static_cast<int>(lo_.size());
        for (int c = 0; c < cols; ++c)
        {
            const float x = x0 + w * (static_cast<float>(c) + 0.5f) / static_cast<float>(cols);
            const float lo = lo_[static_cast<size_t>(c)], hi = hi_[static_cast<size_t>(c)];
            const bool inside = x >= xa && x <= xb;
            g.setColour(inside ? kBlue : kBlue.withAlpha(0.28f));
            g.drawVerticalLine(static_cast<int>(x), mid - hi * half - 0.5f, mid - lo * half + 0.5f);
        }
    }

    // markers
    g.setColour(kGreen);
    g.drawVerticalLine(static_cast<int>(x0 + w * in_), top - 6.0f, bottom);
    g.fillRect(x0 + w * in_ - 1.0f, top - 8.0f, 8.0f, 5.0f);
    g.setColour(kWhite);
    g.drawVerticalLine(static_cast<int>(x0 + w * out_), top - 6.0f, bottom);
    g.fillRect(x0 + w * out_ - 7.0f, top - 8.0f, 8.0f, 5.0f);

    if (head_ >= 0.0f)
    {
        g.setColour(kOrange);
        const float hx = x0 + w * juce::jlimit(0.0f, 1.0f, head_);
        g.drawVerticalLine(static_cast<int>(hx), top - 4.0f, bottom);
    }

    // header: slot, name, details
    g.setColour(kMuted);
    g.setFont(condensed(15.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(juce::String::formatted("SLOT %02d  KEY %d", slot_ + 1, DS_BASE_NOTE + slot_), 12, 8, 150, 22, juce::Justification::centredLeft);
    g.setColour(kInk);
    g.setFont(condensed(22.0f, false));
    juce::String title = sample_ != nullptr ? sample_->name : juce::String();
    g.drawText(title, 150, 8, static_cast<int>(kW) - 330, 22, juce::Justification::centredLeft);

    g.setColour(kMuted);
    g.setFont(mono(11.0f));
    juce::String info;
    if (sample_ != nullptr && sample_->frames() > 0)
        info = juce::String(sample_->frames() / sample_->rate, 2) + " s   " + juce::String(sample_->rate / 1000.0, 1) + " kHz   "
               + (sample_->right.empty() ? "MONO" : "STEREO");
    g.drawText(info, 20, 30, 260, 14, juce::Justification::centredLeft);

    juce::String warn = notice_;
    if (warn.isEmpty() && sample_ != nullptr && sample_->missingPath.isNotEmpty())
        warn = "MISSING " + juce::File(sample_->missingPath).getFileName() + ", USING DEFAULT";
    if (warn.isNotEmpty())
    {
        g.setColour(kOrange);
        g.setFont(mono(11.0f));
        g.drawText(warn, 150, 30, static_cast<int>(kW) - 330, 14, juce::Justification::centredLeft);
    }

    for (const auto& [box, text] : {std::pair{loadBox(), "LOAD"}, std::pair{resetBox(), "DEFAULT"}})
    {
        g.setColour(kTrack);
        g.drawRoundedRectangle(box, 4.0f, 1.0f);
        g.setColour(kMuted);
        g.setFont(condensed(14.0f, true).withExtraKerningFactor(0.2f));
        g.drawText(text, box.toNearestInt(), juce::Justification::centred);
    }
}

void DsWaveform::mouseDown(const juce::MouseEvent& e)
{
    const auto p = toDesign(e.position);
    drag_ = 0;
    if (loadBox().contains(p))
    {
        if (onLoad)
            onLoad();
        return;
    }
    if (resetBox().contains(p))
    {
        if (onReset)
            onReset();
        return;
    }
    const float x0 = 10.0f, w = kW - 20.0f;
    const float dIn = std::abs(p.x - (x0 + w * in_)), dOut = std::abs(p.x - (x0 + w * out_));
    if (std::min(dIn, dOut) < 14.0f && p.y > 24.0f)
        drag_ = dIn <= dOut ? 1 : 2;
    mouseDrag(e);
}

void DsWaveform::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == 0 || !onMarker)
        return;
    const float x0 = 10.0f, w = kW - 20.0f;
    onMarker(drag_ == 1, juce::jlimit(0.0f, 1.0f, (toDesign(e.position).x - x0) / w));
}

void DsWaveform::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto p = toDesign(e.position);
    if (drag_ == 0 && p.y > 40.0f && onLoad)
        onLoad();
}

// ---------------------------------------------------------------- pads

juce::Rectangle<float> DsPadGrid::padRect(int slot) const
{
    const float pw = 94.0f, ph = 56.0f, gx = (kW - 6.0f * pw) / 5.0f, gy = 8.0f;
    const int col = slot % 6, row = slot / 6;
    return {static_cast<float>(col) * (pw + gx), static_cast<float>(row) * (ph + gy), pw, ph};
}

int DsPadGrid::slotAt(juce::Point<int> local) const
{
    const float s = kW / static_cast<float>(getWidth());
    const juce::Point<float> p(static_cast<float>(local.x) * s, static_cast<float>(local.y) * s);
    for (int i = 0; i < DS_SLOTS; ++i)
        if (padRect(i).expanded(2.0f).contains(p))
            return i;
    return -1;
}

void DsPadGrid::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kW));
    for (int i = 0; i < DS_SLOTS; ++i)
    {
        const auto r = padRect(i);
        const bool sel = i == selected_;
        const float fl = flash_[static_cast<size_t>(i)];
        g.setColour(kPanel.withAlpha(0.86f));
        g.fillRoundedRectangle(r, 5.0f);
        if (fl > 0.01f)
        {
            g.setColour(kOrange.withAlpha(0.45f * fl));
            g.fillRoundedRectangle(r, 5.0f);
        }
        if (playing_ & (1u << i))
        {
            g.setColour(kOrange.withAlpha(0.8f));
            g.fillEllipse(r.getRight() - 12.0f, r.getY() + 7.0f, 5.0f, 5.0f);
        }
        const bool drop = i == drop_;
        g.setColour(drop ? kGreen : (sel ? kBlue : kTrack));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, drop || sel ? 1.8f : 1.0f);

        g.setColour(sel ? kBlue : kMuted);
        g.setFont(mono(10.0f));
        g.drawText(juce::String::formatted("%02d", i + 1), r.withTrimmedLeft(8.0f).withHeight(20.0f).toNearestInt(),
                   juce::Justification::centredLeft);
        if (kinds_[static_cast<size_t>(i)] == 1)
        {
            g.setColour(kGreen);
            g.fillEllipse(r.getRight() - 12.0f, r.getBottom() - 12.0f, 5.0f, 5.0f);
        }
        else if (kinds_[static_cast<size_t>(i)] == 2)
        {
            g.setColour(kOrange);
            g.setFont(condensed(14.0f, true));
            g.drawText("!", juce::Rectangle<float>(r.getRight() - 18.0f, r.getBottom() - 22.0f, 14.0f, 18.0f).toNearestInt(),
                       juce::Justification::centred);
        }
        g.setColour(kInk);
        g.setFont(condensed(17.0f, false));
        g.drawText(names_[static_cast<size_t>(i)], r.withTrimmedLeft(8.0f).withTrimmedRight(6.0f).withTrimmedTop(20.0f).toNearestInt(),
                   juce::Justification::centredLeft, true);
    }
}

void DsPadGrid::mouseDown(const juce::MouseEvent& e)
{
    const int slot = slotAt(e.getPosition());
    if (slot < 0)
        return;
    if (onSelect)
        onSelect(slot);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem(1, "Load sample...");
        m.addItem(2, "Use the default sound");
        juce::Component::SafePointer<DsPadGrid> self(this);
        m.showMenuAsync(juce::PopupMenu::Options(), [self, slot](int choice) {
            if (self == nullptr)
                return;
            if (choice == 1 && self->onLoad)
                self->onLoad(slot);
            else if (choice == 2 && self->onReset)
                self->onReset(slot);
        });
        return;
    }
    held_ = slot;
    keys_.noteOn(1, DS_BASE_NOTE + slot, 0.8f);
}

void DsPadGrid::mouseUp(const juce::MouseEvent&)
{
    if (held_ >= 0)
        keys_.noteOff(1, DS_BASE_NOTE + held_, 0.0f);
    held_ = -1;
}

// ---------------------------------------------------------------- editor

DrumSamplerAudioProcessorEditor::DrumSamplerAudioProcessorEditor(DrumSamplerAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      pitch_("PITCH", "SEMITONES", kBlue, DsKnob::Size::Big, semis),
      in_("IN", "START POINT, % OF SAMPLE", kGreen, DsKnob::Size::Big, pct),
      out_("OUT", "END POINT, % OF SAMPLE", kWhite, DsKnob::Size::Big, pct),
      mode_("MODE", "GATE   ONE-SHOT   LOOP", kOrange, DsKnob::Size::Big, modeName),
      attack_("ATK", {}, kBlue, DsKnob::Size::Compact, secs),
      decay_("DEC", {}, kBlue, DsKnob::Size::Compact, secs),
      sustain_("SUS", {}, kBlue, DsKnob::Size::Compact, pct),
      release_("REL", {}, kBlue, DsKnob::Size::Compact, secs),
      level_("OUTPUT", {}, kBlue, DsKnob::Size::Compact, pct),
      gain_("SLOT LVL", {}, kOrange, DsKnob::Size::Compact, dB),
      pads_(p.keyboardState)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&pitch_, &in_, &out_, &mode_, &attack_, &decay_, &sustain_, &release_, &level_, &gain_})
        addAndMakeVisible(*k);
    addAndMakeVisible(wave_);
    addAndMakeVisible(pads_);
    addAndMakeVisible(reverse_);

    attackA_ = std::make_unique<SliderAttachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<SliderAttachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<SliderAttachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<SliderAttachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<SliderAttachment>(processor_.apvts, "level", level_);
    attack_.setDoubleClickReturnValue(true, DS_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, DS_DEFAULT_DECAY);
    sustain_.setDoubleClickReturnValue(true, DS_DEFAULT_SUSTAIN);
    release_.setDoubleClickReturnValue(true, DS_DEFAULT_RELEASE);
    level_.setDoubleClickReturnValue(true, DS_DEFAULT_LEVEL);

    wave_.onMarker = [this](bool isIn, float v) {
        (isIn ? in_ : out_).setValue(v, juce::sendNotificationSync);
    };
    wave_.onLoad = [this] { chooseFile(boundSlot_); };
    wave_.onReset = [this] {
        processor_.resetSlot(boundSlot_);
        refreshSamples();
    };
    pads_.onSelect = [this](int s) { selectSlot(s); };
    pads_.onLoad = [this](int s) { chooseFile(s); };
    pads_.onReset = [this](int s) {
        processor_.resetSlot(s);
        refreshSamples();
    };

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    bindSlot(processor_.selectedSlot());
    refreshSamples();
    startTimerHz(kTimerHz);
}

DrumSamplerAudioProcessorEditor::~DrumSamplerAudioProcessorEditor()
{
    stopTimer();
}

void DrumSamplerAudioProcessorEditor::bindSlot(int slot)
{
    slot = juce::jlimit(0, DS_SLOTS - 1, slot);
    boundSlot_ = slot;
    auto& tree = processor_.apvts;
    pitchA_.reset();
    inA_.reset();
    outA_.reset();
    modeA_.reset();
    gainA_.reset();
    reverseA_.reset();
    pitchA_ = std::make_unique<SliderAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "pitch"), pitch_);
    inA_ = std::make_unique<SliderAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "in"), in_);
    outA_ = std::make_unique<SliderAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "out"), out_);
    modeA_ = std::make_unique<SliderAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "mode"), mode_);
    gainA_ = std::make_unique<SliderAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "gain"), gain_);
    reverseA_ = std::make_unique<ButtonAttachment>(tree, DrumSamplerAudioProcessor::slotParamId(slot, "reverse"), reverse_);
    pitch_.setDoubleClickReturnValue(true, DS_DEFAULT_PITCH);
    in_.setDoubleClickReturnValue(true, DS_DEFAULT_IN);
    out_.setDoubleClickReturnValue(true, DS_DEFAULT_OUT);
    mode_.setDoubleClickReturnValue(true, DS_DEFAULT_MODE);
    gain_.setDoubleClickReturnValue(true, DS_DEFAULT_GAIN);
    pads_.setSelected(slot);
    refreshSamples();
}

void DrumSamplerAudioProcessorEditor::selectSlot(int slot)
{
    processor_.setSelectedSlot(slot);
    bindSlot(slot);
}

void DrumSamplerAudioProcessorEditor::refreshSamples()
{
    std::array<juce::String, DS_SLOTS> names;
    std::array<int, DS_SLOTS> kinds{};
    for (int i = 0; i < DS_SLOTS; ++i)
    {
        const auto s = processor_.sampleFor(i);
        names[static_cast<size_t>(i)] = s != nullptr ? s->name : juce::String();
        kinds[static_cast<size_t>(i)] = s == nullptr ? 0 : (s->missingPath.isNotEmpty() ? 2 : (s->path.isNotEmpty() ? 1 : 0));
    }
    pads_.setNames(names, kinds);
    wave_.setSample(processor_.sampleFor(boundSlot_), boundSlot_);
    lastRevision_ = processor_.sampleRevision();
}

void DrumSamplerAudioProcessorEditor::chooseFile(int slot)
{
    static juce::File lastDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Load a sample into slot " + juce::String(slot + 1), lastDir,
                                                   processor_.fileWildcard());
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::canSelectMultipleItems;
    juce::Component::SafePointer<DrumSamplerAudioProcessorEditor> self(this);
    chooser_->launchAsync(flags, [self, slot](const juce::FileChooser& fc) {
        if (self == nullptr)
            return;
        const auto results = fc.getResults();
        for (int i = 0; i < results.size(); ++i)
        {
            if (i == 0)
                lastDir = results[i].getParentDirectory();
            if (slot + i >= DS_SLOTS)
                break;
            self->loadInto(slot + i, results[i]);
        }
    });
}

void DrumSamplerAudioProcessorEditor::loadInto(int slot, const juce::File& file)
{
    const auto err = processor_.loadFile(slot, file);
    if (err.isNotEmpty())
    {
        wave_.setNotice(err.toUpperCase());
        noticeFrames_ = 4 * kTimerHz;
    }
    refreshSamples();
}

bool DrumSamplerAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File(f).hasFileExtension("wav;wave;aif;aiff;aifc;flac;ogg;mp3;caf"))
            return true;
    return false;
}

void DrumSamplerAudioProcessorEditor::fileDragMove(const juce::StringArray&, int x, int y)
{
    const auto local = pads_.getLocalPoint(this, juce::Point<int>(x, y));
    const int s = pads_.slotAt(local);
    pads_.setDropTarget(s >= 0 ? s : boundSlot_);
}

void DrumSamplerAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    pads_.setDropTarget(-1);
    int slot = pads_.slotAt(pads_.getLocalPoint(this, juce::Point<int>(x, y)));
    if (slot < 0)
        slot = boundSlot_;
    selectSlot(slot);
    for (const auto& f : files)
    {
        if (slot >= DS_SLOTS)
            break;
        if (isInterestedInFileDrag(juce::StringArray(f)))
            loadInto(slot++, juce::File(f));
    }
}

void DrumSamplerAudioProcessorEditor::showPreview(float energy, int flashSlot, float playhead)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    if (flashSlot >= 0 && flashSlot < DS_SLOTS)
    {
        pads_.setFlash(flashSlot, 1.0f);
        pads_.setPlaying(1u << flashSlot);
    }
    wave_.setPlayhead(playhead);
    repaint();
}

void DrumSamplerAudioProcessorEditor::timerCallback()
{
    if (processor_.selectedSlot() != boundSlot_ && !previewHold_)
    {
        bindSlot(processor_.selectedSlot());
        refreshSamples();
    }
    if (processor_.sampleRevision() != lastRevision_)
        refreshSamples();
    if (noticeFrames_ > 0 && --noticeFrames_ == 0)
        wave_.setNotice({});

    wave_.setRegion(static_cast<float>(in_.getValue()), static_cast<float>(out_.getValue()));

    if (!previewHold_)
    {
        const auto a = processor_.takeActivity();
        // Output peaks sit between 0.1 and 0.97, so 1.5x makes an ordinary hit read as about half glow.
        const float level = juce::jlimit(0.0f, 1.0f, a.peak * 1.5f);
        energy_ = level > energy_ ? level : energy_ * 0.92f; // 0.92 per frame is about 200 ms to 1/e
        if (energy_ < 0.002f)
            energy_ = 0.0f;
        for (int s = 0; s < DS_SLOTS; ++s)
        {
            if (a.hits & (1u << s))
                pads_.setFlash(s, 1.0f);
        }
        pads_.setPlaying(a.playing);
        wave_.setPlayhead(a.head[boundSlot_]);
        lastNotes_ = a.notes;
    }
    if (!previewHold_)
        pads_.tickFlash();
    pads_.repaint();
    repaint(juce::Rectangle<float>(kArtX - kArtR, 0.0f, kArtR * 2.0f, kDesignH)
                .transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void DrumSamplerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // Panel behind the envelope row; the waveform and the pads draw their own.
    g.setColour(kPanel.withAlpha(0.86f));
    g.fillRoundedRectangle(kWaveX, kKnobRowY - 4.0f, DsWaveform::kW, kCompactH + 8.0f, 6.0f);
    g.setColour(kTrack);
    g.drawRoundedRectangle(kWaveX + 0.5f, kKnobRowY - 3.5f, DsWaveform::kW - 1.0f, kCompactH + 7.0f, 6.0f, 1.0f);

    // The record glows with the output level. Kept faint on purpose.
    if (energy_ > 0.01f)
    {
        const juce::Point<float> c(kArtX, kArtY);
        juce::ColourGradient grad(kOrange.withAlpha(energy_ * 0.16f), c, kOrange.withAlpha(0.0f), c.translated(kArtR, 0.0f), true);
        g.setGradientFill(grad);
        g.fillEllipse(c.x - kArtR, c.y - kArtR, kArtR * 2.0f, kArtR * 2.0f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.22f));
    g.drawText("DRUM SAMPLER", 48, 24, 400, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(mono(12.0f).withExtraKerningFactor(0.16f));
    g.drawText("24 SLOTS ON THE KEYS", 50, 62, 300, 16, juce::Justification::centredLeft);
}

juce::Rectangle<int> DrumSamplerAudioProcessorEditor::scaled(float x, float y, float w, float h) const
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    return juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt();
}

void DrumSamplerAudioProcessorEditor::resized()
{
    pitch_.setBounds(scaled(60.0f, 110.0f, kBigW, kBigH));
    in_.setBounds(scaled(60.0f, 370.0f, kBigW, kBigH));
    out_.setBounds(scaled(980.0f, 110.0f, kBigW, kBigH));
    mode_.setBounds(scaled(980.0f, 370.0f, kBigW, kBigH));

    wave_.setBounds(scaled(kWaveX, kWaveY, DsWaveform::kW, DsWaveform::kH));
    reverse_.setBounds(scaled(kRevX, kRevY, kRevW, kRevH));
    const float step = DsWaveform::kW / 6.0f;
    DsKnob* row[6] = {&attack_, &decay_, &sustain_, &release_, &level_, &gain_};
    for (int i = 0; i < 6; ++i)
        row[i]->setBounds(scaled(kWaveX + step * static_cast<float>(i) + 0.5f * (step - kCompactW), kKnobRowY, kCompactW, kCompactH));
    pads_.setBounds(scaled(kPadX, kPadY, DsPadGrid::kW, DsPadGrid::kH));
}
