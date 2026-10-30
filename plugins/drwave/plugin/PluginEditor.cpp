#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kBigW = 240.0f, kBigH = 250.0f;
constexpr float kSmallW = 150.0f, kSmallH = 130.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff2ef0a0);
const juce::Colour kWhite(0xffe9ece8);
const juce::Colour kOrange(0xffff9a3d);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

// The wave trace is drawn over the centre of faceplate-bg-1280x630.png.
constexpr float kTraceX0 = 500.0f, kTraceX1 = 780.0f, kTraceY = 470.0f, kTraceH = 70.0f;
constexpr int kTimerHz = 60;

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::String pct(double v) { return juce::String(juce::roundToInt(v * 100.0)); }

juce::String secs(double v)
{
    return v < 1.0 ? juce::String(juce::roundToInt(v * 1000.0)) + " ms" : juce::String(v, 2) + " s";
}
} // namespace

DrWaveKnob::DrWaveKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
               std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), small_(small), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DrWaveKnob::paint(juce::Graphics& g)
{
    const float dw = small_ ? kSmallW : kBigW, dh = small_ ? kSmallH : kBigH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / dw, static_cast<float>(getHeight()) / dh));

    const float cx = dw * 0.5f;
    const float cy = small_ ? 56.0f : 130.0f;
    const float r = small_ ? 26.0f : 74.0f;
    const float stroke = small_ ? 4.0f : 6.2f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    g.setColour(small_ ? kMuted : tone_);
    g.setFont(condensed(small_ ? 20.0f : 36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(dw), small_ ? 22 : 36, juce::Justification::centred);

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

    const float dot = small_ ? 4.5f : 7.0f;
    g.setColour(tone_);
    g.fillEllipse(cx + std::sin(aV) * r - dot, cy - std::cos(aV) * r - dot, dot * 2.0f, dot * 2.0f);

    g.setColour(kInk);
    if (small_)
    {
        g.setFont(condensed(26.0f, false));
        g.drawText(format_(getValue()), 0, 90, static_cast<int>(dw), 30, juce::Justification::centred);
    }
    else
    {
        g.setFont(condensed(76.0f, false));
        g.drawText(format_(getValue()), 0, 92, static_cast<int>(dw), 76, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(dw), 18, juce::Justification::centred);
    }
}

DrWaveAudioProcessorEditor::DrWaveAudioProcessorEditor(DrWaveAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      wave_("WAVE", "POSITION IN THE WAVE BANK", kBlue, false, [](double v) { return juce::String(dw_wave_position(static_cast<float>(v)), 1); }),
      blend_("BLEND", "WAVE TO MIRROR WAVE", kGreen, false, [](double v) { return pct(v); }),
      motion_("MOTION", "SWEEP AT NOTE START", kWhite, false, [](double v) { return pct(v); }),
      fold_("FOLD", "SINE WAVEFOLDER", kOrange, false, [](double v) { return pct(v); }),
      tone_("TONE", {}, kBlue, true, [](double v) { return juce::String(juce::roundToInt(dw_tone_hz(static_cast<float>(v)))) + " Hz"; }),
      attack_("ATK", {}, kBlue, true, secs),
      decay_("DEC", {}, kBlue, true, secs),
      sustain_("SUS", {}, kBlue, true, pct),
      release_("REL", {}, kBlue, true, secs),
      level_("LEVEL", {}, kBlue, true, pct)
{
    dw_init(bank_.get(), 44100.0f);
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&wave_, &blend_, &motion_, &fold_, &tone_, &attack_, &decay_, &sustain_, &release_, &level_})
        addAndMakeVisible(*k);

    wave_.setRange(0.0, 1.0);
    blend_.setRange(0.0, 1.0);
    motion_.setRange(0.0, 1.0);
    fold_.setRange(0.0, 1.0);
    tone_.setRange(0.0, 1.0);
    sustain_.setRange(0.0, 1.0);
    level_.setRange(0.0, 1.0);

    wave_.setDoubleClickReturnValue(true, DW_DEFAULT_WAVE);
    blend_.setDoubleClickReturnValue(true, DW_DEFAULT_BLEND);
    motion_.setDoubleClickReturnValue(true, DW_DEFAULT_MOTION);
    fold_.setDoubleClickReturnValue(true, DW_DEFAULT_FOLD);
    tone_.setDoubleClickReturnValue(true, DW_DEFAULT_TONE);
    sustain_.setDoubleClickReturnValue(true, DW_DEFAULT_SUSTAIN);
    level_.setDoubleClickReturnValue(true, DW_DEFAULT_LEVEL);

    // The time controls use the parameter's own skewed range, which the attachment copies over.
    waveA_ = std::make_unique<Attachment>(processor_.apvts, "wave", wave_);
    blendA_ = std::make_unique<Attachment>(processor_.apvts, "blend", blend_);
    motionA_ = std::make_unique<Attachment>(processor_.apvts, "motion", motion_);
    foldA_ = std::make_unique<Attachment>(processor_.apvts, "fold", fold_);
    toneA_ = std::make_unique<Attachment>(processor_.apvts, "tone", tone_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    attack_.setDoubleClickReturnValue(true, DW_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, DW_DEFAULT_DECAY);
    release_.setDoubleClickReturnValue(true, DW_DEFAULT_RELEASE);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

DrWaveAudioProcessorEditor::~DrWaveAudioProcessorEditor() { stopTimer(); }

void DrWaveAudioProcessorEditor::showGlow(float energy, float pulse)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    pulse_ = pulse;
    repaint();
}

void DrWaveAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const auto a = processor_.takeActivity();
        // Output peaks sit between 0.1 and 0.97, so 1.5x makes an ordinary note read as about half glow.
        const float level = juce::jlimit(0.0f, 1.0f, a.peak * 1.5f);
        energy_ = level > energy_ ? level : energy_ * 0.92f; // 0.92 per frame is about 200 ms to 1/e
        if (energy_ < 0.002f)
            energy_ = 0.0f;
        if (a.notes != lastNotes_)
        {
            lastNotes_ = a.notes;
            pulse_ = 0.0f;
        }
        if (pulse_ >= 0.0f)
        {
            pulse_ += 1.0f / (0.25f * kTimerHz);
            if (pulse_ > 1.4f)
                pulse_ = -1.0f;
        }
    }
    repaint(juce::Rectangle<float>(kTraceX0 - 20.0f, kTraceY - kTraceH - 20.0f, kTraceX1 - kTraceX0 + 40.0f, 2.0f * kTraceH + 40.0f)
                .transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void DrWaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // The trace is the wave the WAVE knob points at (morphed between its two neighbours in the bank), with the
    // BLEND mirror wave behind it. Output level swells the trace a little and lights it; a new note sends a bright
    // spot along it. Kept faint on purpose.
    {
        const float pos = static_cast<float>(wave_.getValue()) * static_cast<float>(DW_WAVES - 1);
        const float blend = static_cast<float>(blend_.getValue());
        auto curve = [this](float position, float amp, juce::Path& path) {
            int w = juce::jlimit(0, DW_WAVES - 2, static_cast<int>(position));
            const float f = position - static_cast<float>(w);
            const int n = 400;
            for (int i = 0; i <= n; ++i)
            {
                const int k = (i * (DW_TABLE_SIZE - 1)) / n;
                const float a = bank_->table[w][2][k], b = bank_->table[w + 1][2][k];
                const float y = (a + f * (b - a)) * 2.4f * amp;
                const float x = kTraceX0 + (kTraceX1 - kTraceX0) * static_cast<float>(i) / static_cast<float>(n);
                if (i == 0) path.startNewSubPath(x, kTraceY - y * kTraceH); else path.lineTo(x, kTraceY - y * kTraceH);
            }
        };
        const float amp = 0.8f + 0.35f * energy_;
        if (blend > 0.02f)
        {
            juce::Path mirror;
            curve(static_cast<float>(DW_WAVES - 1) - pos, amp * 0.8f, mirror);
            g.setColour(kGreen.withAlpha(0.12f + 0.25f * blend));
            g.strokePath(mirror, juce::PathStrokeType(2.0f));
        }
        juce::Path main;
        curve(pos, amp, main);
        g.setColour(kBlue.withAlpha(0.35f + 0.4f * energy_));
        g.strokePath(main, juce::PathStrokeType(2.6f));
        if (pulse_ >= 0.0f)
        {
            const float px = kTraceX0 + (kTraceX1 - kTraceX0) * juce::jlimit(0.0f, 1.0f, pulse_ / 1.2f);
            const float fade = juce::jlimit(0.0f, 1.0f, 1.4f - pulse_);
            juce::ColourGradient grad(kBlue.withAlpha(0.45f * fade), px, kTraceY, kBlue.withAlpha(0.0f), px + 60.0f, kTraceY, true);
            g.setGradientFill(grad);
            g.fillEllipse(px - 60.0f, kTraceY - 60.0f, 120.0f, 120.0f);
        }
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("DR WAVE", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("WAVETABLE VOICE", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void DrWaveAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto big = [s](DrWaveKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kBigW * s, kBigH * s).toNearestInt());
    };
    auto small = [s](DrWaveKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kSmallW * s, kSmallH * s).toNearestInt());
    };
    big(wave_, 60.0f, 110.0f);
    big(blend_, 60.0f, 370.0f);
    big(motion_, 980.0f, 110.0f);
    big(fold_, 980.0f, 370.0f);
    small(tone_, 795.0f, 390.0f);
    small(attack_, 335.0f, 110.0f);
    small(decay_, 335.0f, 250.0f);
    small(level_, 335.0f, 390.0f);
    small(sustain_, 795.0f, 110.0f);
    small(release_, 795.0f, 250.0f);
}
