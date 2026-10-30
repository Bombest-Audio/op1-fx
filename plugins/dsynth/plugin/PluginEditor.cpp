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

// The drum pad in faceplate-bg-1280x630.png: its centre and the radius of the playing surface.
constexpr float kPadX = 640.0f, kPadY = 314.0f, kPadR = 165.0f;
constexpr int kTimerHz = 60;
constexpr float kRippleSeconds = 0.7f;

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

DSynthKnob::DSynthKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
                       std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), small_(small), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void DSynthKnob::paint(juce::Graphics& g)
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
        const auto text = format_(getValue());
        g.setFont(condensed(text.length() > 5 ? 52.0f : 76.0f, false));
        g.drawText(text, 0, 92, static_cast<int>(dw), 76, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(dw), 18, juce::Justification::centred);
    }
}

DSynthAudioProcessorEditor::DSynthAudioProcessorEditor(DSynthAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      mod_("MOD", "FM DEPTH", kBlue, false, [](double v) { return pct(v); }),
      wave_("WAVE", "SINE, SQUARE, SAW", kGreen, false, [](double v) { return pct(v); }),
      decay_("DECAY", "DECAY, THEN ATTACK", kWhite, false,
             [](double v) { return DSynthAudioProcessor_decayText(static_cast<float>(v)); }),
      filter_("FILTER", "MODE 0 TO 7", kOrange, false,
              [](double v) { return juce::String(dsynth_filter_name(juce::roundToInt(v))); }),
      attack_("ATK", {}, kBlue, true, secs),
      ampDecay_("DEC", {}, kBlue, true, secs),
      sustain_("SUS", {}, kBlue, true, pct),
      release_("REL", {}, kBlue, true, secs),
      level_("LEVEL", {}, kBlue, true, pct)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&mod_, &wave_, &decay_, &filter_, &attack_, &ampDecay_, &sustain_, &release_, &level_})
        addAndMakeVisible(*k);

    mod_.setRange(0.0, 1.0);
    wave_.setRange(0.0, 1.0);
    decay_.setRange(0.0, 1.0);
    filter_.setRange(0.0, static_cast<double>(DSYNTH_FILTER_MODES - 1), 1.0);
    sustain_.setRange(0.0, 1.0);
    level_.setRange(0.0, 1.0);

    mod_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_MOD);
    wave_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_WAVE);
    decay_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_DECAY);
    filter_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_FILTER_MODE);
    sustain_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_AMP_SUSTAIN);
    level_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_LEVEL);

    // The time controls use the parameter's own skewed range, which the attachment copies over.
    modA_ = std::make_unique<Attachment>(processor_.apvts, "mod", mod_);
    waveA_ = std::make_unique<Attachment>(processor_.apvts, "wave", wave_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    filterA_ = std::make_unique<Attachment>(processor_.apvts, "filter", filter_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    ampDecayA_ = std::make_unique<Attachment>(processor_.apvts, "ampdecay", ampDecay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    attack_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_AMP_ATTACK);
    ampDecay_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_AMP_DECAY);
    release_.setDoubleClickReturnValue(true, DSYNTH_DEFAULT_AMP_RELEASE);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

DSynthAudioProcessorEditor::~DSynthAudioProcessorEditor() { stopTimer(); }

void DSynthAudioProcessorEditor::showHit(float energy, float ripple)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    ripple_ = ripple;
    repaint();
}

void DSynthAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const auto a = processor_.takeActivity();
        // Output peaks sit between 0.1 and 0.98, so 1.5x makes an ordinary note read as about half glow.
        const float level = juce::jlimit(0.0f, 1.0f, a.peak * 1.5f);
        energy_ = level > energy_ ? level : energy_ * 0.92f; // 0.92 per frame is about 200 ms to 1/e
        if (energy_ < 0.002f)
            energy_ = 0.0f;
        if (a.notes != lastNotes_)
        {
            lastNotes_ = a.notes;
            ripple_ = 0.0f;
        }
        if (ripple_ >= 0.0f)
        {
            ripple_ += 1.0f / (kRippleSeconds * kTimerHz);
            if (ripple_ > 1.0f)
                ripple_ = -1.0f;
        }
    }
    repaint(juce::Rectangle<float>(kPadX - kPadR - 30.0f, kPadY - kPadR - 30.0f, 2.0f * (kPadR + 30.0f), 2.0f * (kPadR + 30.0f))
                .transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void DSynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // The pad glows softly with the output level, and each new note sends one ring from the centre to the rim.
    // Kept faint on purpose.
    if (energy_ > 0.01f)
    {
        juce::ColourGradient glow(kBlue.withAlpha(0.22f * energy_), kPadX, kPadY, kBlue.withAlpha(0.0f), kPadX + kPadR, kPadY, true);
        g.setGradientFill(glow);
        g.fillEllipse(kPadX - kPadR, kPadY - kPadR, 2.0f * kPadR, 2.0f * kPadR);
    }
    if (ripple_ >= 0.0f)
    {
        const float t = ripple_;
        const float radius = 20.0f + (kPadR - 24.0f) * (1.0f - (1.0f - t) * (1.0f - t)); // fast out, slow in at the rim
        const float alpha = 0.55f * (1.0f - t);
        g.setColour(kBlue.brighter(0.4f).withAlpha(alpha));
        g.drawEllipse(kPadX - radius, kPadY - radius, 2.0f * radius, 2.0f * radius, 2.5f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("DSYNTH", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("TWO OSCILLATORS, ONE HIT", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void DSynthAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto big = [s](DSynthKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kBigW * s, kBigH * s).toNearestInt());
    };
    auto small = [s](DSynthKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kSmallW * s, kSmallH * s).toNearestInt());
    };
    big(mod_, 60.0f, 110.0f);
    big(wave_, 60.0f, 370.0f);
    big(decay_, 980.0f, 110.0f);
    big(filter_, 980.0f, 370.0f);
    small(attack_, 315.0f, 110.0f);
    small(ampDecay_, 315.0f, 250.0f);
    small(sustain_, 315.0f, 390.0f);
    small(release_, 815.0f, 110.0f);
    small(level_, 815.0f, 250.0f);
}
