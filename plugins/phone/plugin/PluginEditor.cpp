#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kBlue(0xff6b8cff);
const juce::Colour kMint(0xff2ef0a0);
const juce::Colour kLilac(0xffcfc9ff);
const juce::Colour kRed(0xffff3d5a);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

// The handset art is 630 x 630 at x 325 of the 1280 x 630 faceplate. These are the capsule and chip centres in design space.
constexpr float kArtX = 325.0f, kArtW = 630.0f;
const juce::Point<float> kSpeaker(544.0f, 114.0f); // red capsule, top
const juce::Point<float> kMic(749.0f, 450.0f);     // green capsule, bottom
const juce::Point<float> kChip(728.0f, 237.0f);    // microchip with the blue pot
constexpr int kTimerHz = 60;

juce::String semitones(double norm)
{
    const double st = phone_tone_semitones(static_cast<float>(norm));
    return (st > 0.05 ? "+" : "") + juce::String(st, 1);
}
} // namespace

PhoneKnob::PhoneKnob(juce::String label, juce::String caption, juce::Colour tone,
                     std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void PhoneKnob::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kKnobW,
                                                static_cast<float>(getHeight()) / kKnobH));

    g.setColour(tone_);
    g.setFont(condensed(36.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(kKnobW), 36, juce::Justification::centred);

    const float cx = kKnobW * 0.5f, cy = 130.0f, r = 74.0f;
    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    juce::Path track;
    track.addCentredArc(cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (prop > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.0f, a0, aV, true);
        g.setColour(tone_);
        g.strokePath(arc, juce::PathStrokeType(6.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float tx = cx + std::sin(aV) * r;
    const float ty = cy - std::cos(aV) * r;
    g.setColour(tone_);
    g.fillEllipse(tx - 7.0f, ty - 7.0f, 14.0f, 14.0f);

    g.setColour(kInk);
    g.setFont(condensed(76.0f, false));
    g.drawText(format_(getValue()), 0, 92, static_cast<int>(kKnobW), 76, juce::Justification::centred);

    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText(caption_, 0, 222, static_cast<int>(kKnobW), 18, juce::Justification::centred);
}

PhoneAudioProcessorEditor::PhoneAudioProcessorEditor(PhoneAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      tone_("TONE", "PITCH, SEMITONES", kBlue, [](double v) { return semitones(v); }),
      phonic_("PHONIC", "CYCLES PER FRAGMENT", kMint, [](double v) { return juce::String(juce::roundToInt(v)); }),
      baud_("BAUD", "LOOPS PER FRAGMENT", kLilac, [](double v) { return juce::String(juce::roundToInt(v)); }),
      telematic_("TELEMATIC", "LINE MIX", kRed, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&tone_, &phonic_, &baud_, &telematic_})
        addAndMakeVisible(*k);

    tone_.setRange(0.0, 1.0);
    telematic_.setRange(0.0, 1.0);
    phonic_.setRange(1.0, static_cast<double>(PHONE_MAX_CYCLES), 1.0);
    baud_.setRange(1.0, static_cast<double>(PHONE_MAX_REPEATS), 1.0);

    tone_.setDoubleClickReturnValue(true, PHONE_DEFAULT_TONE);
    telematic_.setDoubleClickReturnValue(true, PHONE_DEFAULT_TELEMATIC);
    phonic_.setDoubleClickReturnValue(true, PHONE_DEFAULT_PHONIC);
    baud_.setDoubleClickReturnValue(true, PHONE_DEFAULT_BAUD);

    toneA_ = std::make_unique<Attachment>(processor_.apvts, "tone", tone_);
    phonicA_ = std::make_unique<Attachment>(processor_.apvts, "phonic", phonic_);
    baudA_ = std::make_unique<Attachment>(processor_.apvts, "baud", baud_);
    telematicA_ = std::make_unique<Attachment>(processor_.apvts, "telematic", telematic_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

PhoneAudioProcessorEditor::~PhoneAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> PhoneAudioProcessorEditor::artRegion()
{
    return {kArtX, 0.0f, kArtW, kDesignH};
}

void PhoneAudioProcessorEditor::showLevels(float mic, float chipFlash)
{
    previewHold_ = true;
    mic_ = juce::jlimit(0.0f, 1.0f, mic);
    speaker_ = mic_;
    chip_ = juce::jlimit(0.0f, 1.0f, chipFlash);
    phase_ = 0.35;
    repaint();
}

void PhoneAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const float level = juce::jlimit(0.0f, 1.0f, processor_.takeActivity());
        // The mic rises with the input at once and falls with 0.9 per frame, about 95 ms to 1/e.
        mic_ = level > mic_ ? level : mic_ * 0.9f;
        if (mic_ < 0.002f)
            mic_ = 0.0f;
        // The speaker lags the mic and is quieter at a low TELEMATIC setting.
        const float mix = static_cast<float>(telematic_.getValue());
        speaker_ += 0.12f * (mic_ * (0.35f + 0.65f * mix) - speaker_);
        if (speaker_ < 0.002f)
            speaker_ = 0.0f;
        // Each fragment change flashes the chip.
        if (processor_.takeSwaps() > 0)
            chip_ = 1.0f;
        else
            chip_ *= 0.86f;
        if (chip_ < 0.01f)
            chip_ = 0.0f;
        phase_ += juce::MathConstants<double>::twoPi / (1.4 * kTimerHz);
    }
    repaint(artRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer());
}

void PhoneAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    // Signal-driven overlay. All of it is zero when nothing plays, so a silent plugin shows the plain art.
    auto glow = [&g](juce::Point<float> c, juce::Colour col, float radius, float alpha) {
        juce::ColourGradient grad(col.withAlpha(alpha), c, col.withAlpha(0.0f), c.translated(radius, 0.0f), true);
        g.setGradientFill(grad);
        g.fillEllipse(c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    };
    // Three arcs travel along the capsule's axis. `outward` sends them away from the capsule (speaker), otherwise toward it (mic).
    auto waves = [&](juce::Point<float> c, juce::Colour col, float level, float centreDeg, bool outward) {
        if (level < 0.01f)
            return;
        const float spread = juce::degreesToRadians(52.0f);
        const float mid = juce::degreesToRadians(centreDeg);
        for (int k = 0; k < 3; ++k)
        {
            float t = std::fmod(static_cast<float>(phase_) / juce::MathConstants<float>::twoPi + static_cast<float>(k) / 3.0f, 1.0f);
            if (!outward)
                t = 1.0f - t;
            const float r = 54.0f + 56.0f * t;
            const float fade = std::sin(juce::MathConstants<float>::pi * (outward ? t : 1.0f - t)); // 0 at both ends
            juce::Path arc;
            arc.addCentredArc(c.x, c.y, r, r, 0.0f, mid - spread, mid + spread, true);
            g.setColour(col.withAlpha(0.55f * level * fade));
            g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    glow(kMic, kMint, 70.0f, 0.30f * mic_);
    glow(kSpeaker, kRed, 78.0f, 0.34f * speaker_);
    waves(kMic, kMint, mic_, 140.0f, false);
    waves(kSpeaker, kRed, speaker_, -40.0f, true);
    glow(kChip, kBlue, 58.0f, 0.38f * chip_);

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("PHONE", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("CYCLE LOOPER", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void PhoneAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](PhoneKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(tone_, 60.0f, 110.0f);
    place(phonic_, 60.0f, 370.0f);
    place(baud_, 980.0f, 110.0f);
    place(telematic_, 980.0f, 370.0f);
}
