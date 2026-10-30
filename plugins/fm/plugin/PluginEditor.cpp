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

// Centres of the four cubes in faceplate-bg-1280x630.png, top (modulator) to bottom (carrier).
constexpr float kCubeX = 640.0f;
constexpr float kCubeY[4] = {66.0f, 206.0f, 362.0f, 532.0f};
constexpr float kCubeRadius = 130.0f;
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

FmKnob::FmKnob(juce::String label, juce::String caption, juce::Colour tone, bool small,
               std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), small_(small), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void FmKnob::paint(juce::Graphics& g)
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

FmAudioProcessorEditor::FmAudioProcessorEditor(FmAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      ratio_("RATIO", "MODULATOR RATIO", kBlue, false, [](double v) { return juce::String(fm_ratio_value(static_cast<float>(v)), 2); }),
      index_("INDEX", "MODULATION DEPTH", kGreen, false, [](double v) { return pct(v); }),
      feedback_("FEEDBACK", "MODULATOR SELF-FEEDBACK", kWhite, false, [](double v) { return pct(v); }),
      layer_("LAYER", "OCTAVE-UP PAIR", kOrange, false, [](double v) { return pct(v); }),
      attack_("ATK", {}, kBlue, true, secs),
      decay_("DEC", {}, kBlue, true, secs),
      sustain_("SUS", {}, kBlue, true, pct),
      release_("REL", {}, kBlue, true, secs),
      level_("LEVEL", {}, kBlue, true, pct)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&ratio_, &index_, &feedback_, &layer_, &attack_, &decay_, &sustain_, &release_, &level_})
        addAndMakeVisible(*k);

    ratio_.setRange(0.0, 1.0);
    index_.setRange(0.0, 1.0);
    feedback_.setRange(0.0, 1.0);
    layer_.setRange(0.0, 1.0);
    sustain_.setRange(0.0, 1.0);
    level_.setRange(0.0, 1.0);

    ratio_.setDoubleClickReturnValue(true, FM_DEFAULT_RATIO);
    index_.setDoubleClickReturnValue(true, FM_DEFAULT_INDEX);
    feedback_.setDoubleClickReturnValue(true, FM_DEFAULT_FEEDBACK);
    layer_.setDoubleClickReturnValue(true, FM_DEFAULT_LAYER);
    sustain_.setDoubleClickReturnValue(true, FM_DEFAULT_SUSTAIN);
    level_.setDoubleClickReturnValue(true, FM_DEFAULT_LEVEL);

    // The time controls use the parameter's own skewed range, which the attachment copies over.
    ratioA_ = std::make_unique<Attachment>(processor_.apvts, "ratio", ratio_);
    indexA_ = std::make_unique<Attachment>(processor_.apvts, "index", index_);
    feedbackA_ = std::make_unique<Attachment>(processor_.apvts, "feedback", feedback_);
    layerA_ = std::make_unique<Attachment>(processor_.apvts, "layer", layer_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    attack_.setDoubleClickReturnValue(true, FM_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, FM_DEFAULT_DECAY);
    release_.setDoubleClickReturnValue(true, FM_DEFAULT_RELEASE);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

FmAudioProcessorEditor::~FmAudioProcessorEditor() { stopTimer(); }

void FmAudioProcessorEditor::showGlow(float energy, float pulse)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    pulse_ = pulse;
    repaint();
}

void FmAudioProcessorEditor::timerCallback()
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
    repaint(juce::Rectangle<float>(kCubeX - 200.0f, 0.0f, 400.0f, kDesignH)
                .transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void FmAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // The four cubes are the four operators: the top one modulates the one below it. Output level lights all of
    // them softly, and a new note sends a pulse down the chain from modulator to carrier. Kept faint on purpose.
    for (int i = 0; i < 4; ++i)
    {
        float glow = 0.35f * energy_;
        if (pulse_ >= 0.0f)
        {
            const float d = (pulse_ - static_cast<float>(i) / 3.0f) / 0.18f;
            glow += 0.55f * std::exp(-d * d);
        }
        if (glow < 0.01f)
            continue;
        const juce::Point<float> c(kCubeX, kCubeY[i]);
        juce::ColourGradient grad(kBlue.withAlpha(juce::jmin(glow, 1.0f) * 0.34f), c, kBlue.withAlpha(0.0f),
                                  c.translated(kCubeRadius, 0.0f), true);
        g.setGradientFill(grad);
        g.fillEllipse(c.x - kCubeRadius, c.y - kCubeRadius, kCubeRadius * 2.0f, kCubeRadius * 2.0f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("FM", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("FOUR OPERATORS", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void FmAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto big = [s](FmKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kBigW * s, kBigH * s).toNearestInt());
    };
    auto small = [s](FmKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kSmallW * s, kSmallH * s).toNearestInt());
    };
    big(ratio_, 60.0f, 110.0f);
    big(index_, 60.0f, 370.0f);
    big(feedback_, 980.0f, 110.0f);
    big(layer_, 980.0f, 370.0f);
    small(attack_, 335.0f, 110.0f);
    small(decay_, 335.0f, 250.0f);
    small(level_, 335.0f, 390.0f);
    small(sustain_, 795.0f, 110.0f);
    small(release_, 795.0f, 250.0f);
}
