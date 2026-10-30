#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kBigW = 240.0f, kBigH = 250.0f;
constexpr float kSmallW = 118.0f, kSmallH = 110.0f;

const juce::Colour kBlue(0xff5aa0ff);
const juce::Colour kMint(0xff2ef0a0);
const juce::Colour kOrange(0xffff9a3d);
const juce::Colour kAmber(0xffffb347);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kEnv(0xff8a928e);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

// The strings panel inside the 1280x630 faceplate. The strings run from the bridge up to a vanishing point.
constexpr float kArtX = 330.0f, kArtW = 620.0f;
constexpr float kBridgeY = 395.0f, kTopY = 20.0f;
constexpr float kMaxSwing = 7.0f;     // design pixels of sideways travel at the middle of the strings
constexpr float kStripH = 3.0f;
constexpr float kDotsY = 458.0f;
constexpr int kTimerHz = 60;
constexpr float kVisualHz = 9.0f;

juce::String seconds(double s)
{
    if (s < 0.995)
        return juce::String(juce::roundToInt(s * 1000.0)) + "ms";
    return juce::String(s, s < 9.95 ? 1 : 0) + "s";
}
} // namespace

StringKnob::StringKnob(juce::String label, juce::String caption, juce::Colour tone,
                       std::function<juce::String(double)> format, bool small)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format)), small_(small)
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void StringKnob::paint(juce::Graphics& g)
{
    const float cw = small_ ? kSmallW : kBigW, ch = small_ ? kSmallH : kBigH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / cw, static_cast<float>(getHeight()) / ch));

    const float cx = cw * 0.5f;
    const float cy = small_ ? 62.0f : 130.0f;
    const float r = small_ ? 30.0f : 74.0f;
    const float stroke = small_ ? 4.0f : 6.2f;
    const float dot = small_ ? 4.0f : 7.0f;

    g.setColour(small_ ? juce::Colour(0xffa9afab) : tone_);
    g.setFont(condensed(small_ ? 18.0f : 36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(cw), small_ ? 18 : 36, juce::Justification::centred);

    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

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

    const float tx = cx + std::sin(aV) * r;
    const float ty = cy - std::cos(aV) * r;
    g.setColour(tone_);
    g.fillEllipse(tx - dot, ty - dot, dot * 2.0f, dot * 2.0f);

    g.setColour(kInk);
    if (small_)
    {
        g.setFont(condensed(30.0f, false));
        g.drawText(format_(getValue()), 0, 44, static_cast<int>(cw), 36, juce::Justification::centred);
    }
    else
    {
        g.setFont(condensed(76.0f, false));
        g.drawText(format_(getValue()), 0, 92, static_cast<int>(cw), 76, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 222, static_cast<int>(cw), 18, juce::Justification::centred);
    }
}

StringAudioProcessorEditor::StringAudioProcessorEditor(StringAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      pick_("PICK", "PLUCK POSITION, % OF STRING", kBlue,
            [](double v) { return juce::String(juce::roundToInt(opst_pick_position(static_cast<float>(v)) * 100.0f)); }),
      damp_("DAMP", "LOOP BRIGHTNESS", kMint, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      stiff_("STIFF", "STRING STIFFNESS", kInk, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      decay_("DECAY", "RING TIME TO -60 DB", kOrange,
             [](double v) { return seconds(opst_decay_seconds(static_cast<float>(v))); }),
      attack_("ATTACK", "", kEnv, [](double v) { return seconds(stringtime::toSeconds(static_cast<float>(v), stringtime::kAttackLo, stringtime::kAttackHi)); }, true),
      envDecay_("DECAY", "", kEnv, [](double v) { return seconds(stringtime::toSeconds(static_cast<float>(v), stringtime::kLongLo, stringtime::kLongHi)); }, true),
      sustain_("SUSTAIN", "", kEnv, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }, true),
      release_("RELEASE", "", kEnv, [](double v) { return seconds(stringtime::toSeconds(static_cast<float>(v), stringtime::kLongLo, stringtime::kLongHi)); }, true),
      level_("LEVEL", "", kInk,
             [](double v) { return v <= -59.9 ? juce::String("off") : juce::String(juce::roundToInt(v)) + "dB"; }, true)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png, BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&pick_, &damp_, &stiff_, &decay_, &attack_, &envDecay_, &sustain_, &release_, &level_})
        addAndMakeVisible(*k);

    pick_.setDoubleClickReturnValue(true, OPST_DEFAULT_PICK);
    damp_.setDoubleClickReturnValue(true, OPST_DEFAULT_DAMP);
    stiff_.setDoubleClickReturnValue(true, OPST_DEFAULT_STIFF);
    decay_.setDoubleClickReturnValue(true, OPST_DEFAULT_DECAY);
    attack_.setDoubleClickReturnValue(true, stringtime::toNorm(OPST_DEFAULT_ATTACK, stringtime::kAttackLo, stringtime::kAttackHi));
    envDecay_.setDoubleClickReturnValue(true, stringtime::toNorm(OPST_DEFAULT_ENV_DECAY, stringtime::kLongLo, stringtime::kLongHi));
    sustain_.setDoubleClickReturnValue(true, OPST_DEFAULT_SUSTAIN);
    release_.setDoubleClickReturnValue(true, stringtime::toNorm(OPST_DEFAULT_RELEASE, stringtime::kLongLo, stringtime::kLongHi));
    level_.setDoubleClickReturnValue(true, 0.0);

    // The attachments take each slider's range and skew from its parameter.
    pickA_ = std::make_unique<Attachment>(processor_.apvts, "pick", pick_);
    dampA_ = std::make_unique<Attachment>(processor_.apvts, "damp", damp_);
    stiffA_ = std::make_unique<Attachment>(processor_.apvts, "stiff", stiff_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    envDecayA_ = std::make_unique<Attachment>(processor_.apvts, "env_decay", envDecay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    art_ = background_.getClippedImage(juce::Rectangle<int>(juce::roundToInt(kArtX), 0, juce::roundToInt(kArtW),
                                                             static_cast<int>(kDesignH))).createCopy();
    dotOn_.fill(false);
    startTimerHz(kTimerHz);
}

StringAudioProcessorEditor::~StringAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> StringAudioProcessorEditor::artRegion()
{
    return {kArtX, 0.0f, kArtW, kDesignH};
}

void StringAudioProcessorEditor::showVibration(float strength)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, strength);
    phase_ = 1.1;
    for (size_t i = 0; i < OPST_VOICES; ++i)
    {
        dotOn_[i] = i < 5;
        dots_[i] = i < 5 ? 0.25f + 0.2f * static_cast<float>(i) : 0.0f;
    }
    repaint();
}

void StringAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const auto act = processor_.takeActivity();
        // Rise at once, fall exponentially: 0.92 per frame is about 200 ms to 1/e.
        energy_ = juce::jmax(juce::jmin(act.level * 2.0f, 1.0f), energy_ * 0.92f);
        if (energy_ < 0.002f)
            energy_ = 0.0f;
        for (size_t i = 0; i < OPST_VOICES; ++i)
        {
            dotOn_[i] = act.voiceNote[i] >= 0;
            dots_[i] = juce::jmax(juce::jmin(act.voice[i] * 5.0f, 1.0f), dots_[i] * 0.9f);
        }
        if (energy_ > 0.0f)
            phase_ += juce::MathConstants<double>::twoPi * kVisualHz / kTimerHz;
    }
    repaint(artRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer());
}

void StringAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    if (art_.isValid() && energy_ > 0.004f)
    {
        // Redraw the strings panel in thin strips, each shifted sideways by a standing wave: zero at the bridge and at
        // the far end, widest in the middle. The motion is the output level, drawn at a calm visual rate.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(artRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        g.fillAll(kPlate);
        const float swing = kMaxSwing * std::sqrt(energy_) * static_cast<float>(std::sin(phase_));
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        for (float y = 0.0f; y < kDesignH; y += kStripH)
        {
            float dx = 0.0f;
            if (y >= kTopY && y <= kBridgeY)
                dx = swing * std::sin(juce::MathConstants<float>::pi * (kBridgeY - y) / (kBridgeY - kTopY));
            const int y0 = juce::roundToInt(y * s), y1 = juce::roundToInt((y + kStripH) * s);
            if (y1 > y0)
                g.drawImage(art_, juce::roundToInt((kArtX + dx) * s), y0, juce::roundToInt(kArtW * s), y1 - y0, 0,
                            static_cast<int>(y), static_cast<int>(kArtW), static_cast<int>(kStripH));
        }
        // a warm glow at the bridge that follows the level
        juce::ColourGradient glow(kAmber.withAlpha(0.16f * energy_), kArtX * s + kArtW * 0.5f * s, kBridgeY * s,
                                  kAmber.withAlpha(0.0f), kArtX * s + kArtW * 0.5f * s, (kBridgeY - 230.0f) * s, true);
        g.setGradientFill(glow);
        g.fillRect(artRegion().transformedBy(juce::AffineTransform::scale(s)));
    }

    g.addTransform(juce::AffineTransform::scale(s));

    // One light per voice under the bridge.
    for (size_t i = 0; i < OPST_VOICES; ++i)
    {
        const float x = 420.0f + 440.0f * static_cast<float>(i) / (OPST_VOICES - 1);
        if (dotOn_[i])
        {
            const float a = 0.25f + 0.75f * std::sqrt(juce::jlimit(0.0f, 1.0f, dots_[i]));
            g.setColour(kAmber.withAlpha(a));
            g.fillEllipse(x - 4.0f, kDotsY - 4.0f, 8.0f, 8.0f);
        }
        else
        {
            g.setColour(kTrack);
            g.fillEllipse(x - 3.0f, kDotsY - 3.0f, 6.0f, 6.0f);
        }
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("STRING", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("PLUCKED STRING", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void StringAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](StringKnob& k, float x, float y, float w, float h) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(pick_, 60.0f, 110.0f, kBigW, kBigH);
    place(damp_, 60.0f, 370.0f, kBigW, kBigH);
    place(stiff_, 980.0f, 110.0f, kBigW, kBigH);
    place(decay_, 980.0f, 370.0f, kBigW, kBigH);
    place(attack_, 345.0f, 505.0f, kSmallW, kSmallH);
    place(envDecay_, 463.0f, 505.0f, kSmallW, kSmallH);
    place(sustain_, 581.0f, 505.0f, kSmallW, kSmallH);
    place(release_, 699.0f, 505.0f, kSmallW, kSmallH);
    place(level_, 817.0f, 505.0f, kSmallW, kSmallH);
}
