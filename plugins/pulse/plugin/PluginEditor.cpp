#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kBigW = 240.0f, kBigH = 250.0f;
constexpr float kSmallW = 128.0f, kSmallH = 120.0f;

// OP-1 screen colors for the four engine knobs: blue, green, white, orange.
const juce::Colour kBlue(0xff4d9eff);
const juce::Colour kGreen(0xff00ed95);
const juce::Colour kWhite(0xffdfd9ff);
const juce::Colour kOrange(0xffff8a1f);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);
const juce::Colour kNeon(0xff3fd2ff);

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

constexpr int kTimerHz = 60;
// The neon tube inside the faceplate art, in design pixels.
const juce::Rectangle<float> kTube(330.0f, 150.0f, 620.0f, 170.0f);
const juce::Rectangle<float> kDots(48.0f, 82.0f, 140.0f, 14.0f);

juce::String timeText(double seconds)
{
    if (seconds < 0.01)
        return juce::String(seconds * 1000.0, 1) + "ms";
    if (seconds < 1.0)
        return juce::String(juce::roundToInt(seconds * 1000.0)) + "ms";
    return juce::String(seconds, 1) + "s";
}
} // namespace

PulseKnob::PulseKnob(juce::String label, juce::String caption, juce::Colour tone, bool small, bool bipolar,
                     std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), small_(small), bipolar_(bipolar),
      format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void PulseKnob::paint(juce::Graphics& g)
{
    const float dw = small_ ? kSmallW : kBigW, dh = small_ ? kSmallH : kBigH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / dw, static_cast<float>(getHeight()) / dh));

    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    const float cx = dw * 0.5f;
    const float cy = small_ ? 74.0f : 130.0f;
    const float r = small_ ? 28.0f : 74.0f;
    const float stroke = small_ ? 4.0f : 6.2f;

    g.setColour(tone_);
    if (small_)
        g.setFont(condensed(22.0f, true).withExtraKerningFactor(0.18f));
    else
        g.setFont(condensed(36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(dw), small_ ? 24 : 36, juce::Justification::centred);

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float aStart = bipolar_ ? 0.0f : a0;
    if (std::abs(aV - aStart) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, juce::jmin(aStart, aV), juce::jmax(aStart, aV), true);
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
        g.drawText(format_(getValue()), 0, static_cast<int>(cy - 17.0f), static_cast<int>(dw), 34, juce::Justification::centred);
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

PulseAudioProcessorEditor::PulseAudioProcessorEditor(PulseAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      shape_("SHAPE", "PULSE DUTY PERCENT", kBlue, false, false,
             [](double v) { return juce::String(juce::roundToInt(pulse_duty_pct(static_cast<float>(v)))); }),
      sweep_("SWEEP", "PWM DEPTH", kGreen, false, false, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      rate_("RATE", "PWM HZ", kWhite, false, false,
            [](double v) {
                const double hz = pulse_rate_hz(static_cast<float>(v));
                return hz >= 10.0 ? juce::String(juce::roundToInt(hz)) : juce::String(hz, 1);
            }),
      detune_("DETUNE", "CENTS BETWEEN OSCS", kOrange, false, true,
              [](double v) { return juce::String(juce::roundToInt(pulse_detune_cents(static_cast<float>(v)))); }),
      attack_("ATTACK", "", kMuted, true, false, [](double v) { return timeText(pulse_attack_s(static_cast<float>(v))); }),
      decay_("DECAY", "", kMuted, true, false, [](double v) { return timeText(pulse_decay_s(static_cast<float>(v))); }),
      sustain_("SUSTAIN", "", kMuted, true, false, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      release_("RELEASE", "", kMuted, true, false, [](double v) { return timeText(pulse_release_s(static_cast<float>(v))); }),
      level_("LEVEL", "", kMuted, true, false, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png, BinaryData::faceplatebg1280x630_pngSize);

    struct Row { PulseKnob* k; double lo, hi, def; };
    const Row rows[] = {{&shape_, 0, 1, PULSE_DEFAULT_SHAPE},     {&sweep_, 0, 1, PULSE_DEFAULT_SWEEP},
                        {&rate_, 0, 1, PULSE_DEFAULT_RATE},       {&detune_, -1, 1, PULSE_DEFAULT_DETUNE},
                        {&attack_, 0, 1, PULSE_DEFAULT_ATTACK},   {&decay_, 0, 1, PULSE_DEFAULT_DECAY},
                        {&sustain_, 0, 1, PULSE_DEFAULT_SUSTAIN}, {&release_, 0, 1, PULSE_DEFAULT_RELEASE},
                        {&level_, 0, 1, PULSE_DEFAULT_LEVEL}};
    for (const auto& row : rows)
    {
        addAndMakeVisible(*row.k);
        row.k->setRange(row.lo, row.hi);
        row.k->setDoubleClickReturnValue(true, row.def);
    }

    shapeA_ = std::make_unique<Attachment>(processor_.apvts, "shape", shape_);
    sweepA_ = std::make_unique<Attachment>(processor_.apvts, "sweep", sweep_);
    rateA_ = std::make_unique<Attachment>(processor_.apvts, "rate", rate_);
    detuneA_ = std::make_unique<Attachment>(processor_.apvts, "detune", detune_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);
    startTimerHz(kTimerHz);
}

PulseAudioProcessorEditor::~PulseAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<int> PulseAudioProcessorEditor::toPixels(juce::Rectangle<float> r) const
{
    return r.transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer();
}

void PulseAudioProcessorEditor::showActivity(float level, int voices)
{
    previewHold_ = true;
    glow_ = juce::jlimit(0.0f, 1.0f, level);
    voices_ = juce::jlimit(0, PULSE_VOICES, voices);
    repaint();
}

void PulseAudioProcessorEditor::timerCallback()
{
    if (previewHold_)
        return;
    // Output peak runs to about 0.5 for one voice at the default level, so scale it up. Rise instantly, fall at 0.94 per frame
    // (about 250 ms to 1/e).
    const float lvl = juce::jlimit(0.0f, 1.0f, processor_.takeLevel() * 2.0f);
    const float before = glow_;
    const int vBefore = voices_;
    glow_ = lvl > glow_ ? lvl : glow_ * 0.94f;
    if (glow_ < 0.004f)
        glow_ = 0.0f;
    voices_ = processor_.activeVoices();
    if (std::abs(glow_ - before) > 0.002f)
        repaint(toPixels(kTube));
    if (voices_ != vBefore)
        repaint(toPixels(kDots));
}

void PulseAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    if (glow_ > 0.004f)
    {
        // A cyan bloom over the neon tube that follows the output level. The circular gradient is squashed to an ellipse
        // by a vertical scale, so it fades to nothing before its edge.
        const auto c = kTube.getCentre();
        const float radius = kTube.getWidth() * 0.55f;
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::scale(1.0f, 0.3f, c.x, c.y));
        juce::ColourGradient bloom(kNeon.withAlpha(0.26f * glow_), c, kNeon.withAlpha(0.0f), c.translated(radius, 0.0f), true);
        g.setGradientFill(bloom);
        g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("PULSE", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("PULSE-WIDTH SYNTH", 50, 62, 300, 16, juce::Justification::centredLeft);

    // Eight voice dots: lit ones are sounding.
    for (int i = 0; i < PULSE_VOICES; ++i)
    {
        g.setColour(i < voices_ ? kNeon : kTrack);
        g.fillEllipse(50.0f + 16.0f * static_cast<float>(i), 86.0f, 7.0f, 7.0f);
    }
}

void PulseAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](PulseKnob& k, float x, float y, float w, float h) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(shape_, 60.0f, 110.0f, kBigW, kBigH);
    place(sweep_, 60.0f, 370.0f, kBigW, kBigH);
    place(rate_, 980.0f, 110.0f, kBigW, kBigH);
    place(detune_, 980.0f, 370.0f, kBigW, kBigH);

    float x = 320.0f;
    for (auto* k : {&attack_, &decay_, &sustain_, &release_, &level_})
    {
        place(*k, x, 500.0f, kSmallW, kSmallH);
        x += kSmallW;
    }
}
