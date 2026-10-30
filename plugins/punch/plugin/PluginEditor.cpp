#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kViolet(0xffa77bff);
const juce::Colour kRed(0xffff4d5e);
const juce::Colour kMint(0xff2ef0a0);
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
constexpr float kBoxerX = 330.0f, kBoxerW = 620.0f;  // the boxer panel inside the 1280x630 faceplate
constexpr float kPivotX = 640.0f, kPivotY = 400.0f;  // he lunges from the hips, so the head moves most
constexpr int kTimerHz = 60;
// Round 0 reports about 1.0 for a clear hit and 3.0 for a hard click. 1.5 and up reads as a full punch.
constexpr float kFullPunchActivity = 1.5f;
} // namespace

PunchKnob::PunchKnob(juce::String label, juce::String caption, juce::Colour tone,
                     std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void PunchKnob::paint(juce::Graphics& g)
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

PunchAudioProcessorEditor::PunchAudioProcessorEditor(PunchAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      freq_("FREQ", "SIDECHAIN HZ", kViolet,
            [](double v) {
                const double hz = punch_freq_hz(static_cast<float>(v));
                return hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + "k" : juce::String(juce::roundToInt(hz));
            }),
      punch_("PUNCH", "TRANSIENT PUSH", kRed, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      rounds_("ROUNDS", "STAGES IN THE STACK", kMint, [](double v) { return juce::String(juce::roundToInt(v)); }),
      power_("POWER", "DRIVE AND MIX", kRed, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&freq_, &punch_, &rounds_, &power_})
        addAndMakeVisible(*k);

    freq_.setRange(0.0, 1.0);
    punch_.setRange(0.0, 1.0);
    power_.setRange(0.0, 1.0);
    rounds_.setRange(1.0, static_cast<double>(PUNCH_MAX_ROUNDS), 1.0);

    freq_.setDoubleClickReturnValue(true, PUNCH_DEFAULT_FREQ);
    punch_.setDoubleClickReturnValue(true, PUNCH_DEFAULT_PUNCH);
    power_.setDoubleClickReturnValue(true, PUNCH_DEFAULT_POWER);
    rounds_.setDoubleClickReturnValue(true, PUNCH_DEFAULT_ROUNDS);

    freqA_ = std::make_unique<Attachment>(processor_.apvts, "freq", freq_);
    punchA_ = std::make_unique<Attachment>(processor_.apvts, "punch", punch_);
    roundsA_ = std::make_unique<Attachment>(processor_.apvts, "rounds", rounds_);
    powerA_ = std::make_unique<Attachment>(processor_.apvts, "power", power_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    boxer_ = background_.getClippedImage(juce::Rectangle<int>(juce::roundToInt(kBoxerX), 0, juce::roundToInt(kBoxerW),
                                                                static_cast<int>(kDesignH)));
    startTimerHz(kTimerHz);
}

PunchAudioProcessorEditor::~PunchAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> PunchAudioProcessorEditor::boxerRegion()
{
    return {kBoxerX, 0.0f, kBoxerW, kDesignH};
}

void PunchAudioProcessorEditor::showPunch(float strength)
{
    previewHold_ = true;
    lunge_ = swing_ = juce::jlimit(0.0f, 1.0f, strength);
    repaint();
}

void PunchAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const float hit = juce::jlimit(0.0f, 1.0f, processor_.takeActivity() / kFullPunchActivity);
        // Rise instantly so the punch lands with the transient. Fall exponentially: 0.88 per frame is about 130 ms to 1/e.
        lunge_ = hit > lunge_ ? hit : lunge_ * 0.88f;
        if (lunge_ < 0.002f)
            lunge_ = 0.0f;
        swing_ += 0.22f * (lunge_ - swing_);
        phase_ += juce::MathConstants<double>::twoPi / (2.6 * kTimerHz);
    }
    repaint(boxerRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer());
}

void PunchAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    if (boxer_.isValid())
    {
        // Idle breathing, then the punch: a lunge toward the viewer with a small recoil shake that fades with the hit.
        const float breath = 0.004f * static_cast<float>(std::sin(phase_));
        const float zoom = 1.0f + breath + 0.035f * lunge_ + 0.015f * swing_;
        const float lift = -6.0f * lunge_ + 1.5f * static_cast<float>(std::sin(phase_));
        const float shake = 2.5f * lunge_ * std::sin(static_cast<float>(phase_) * 40.0f);

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(boxerRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        const auto place = juce::AffineTransform::scale(s).translated(kBoxerX * s, 0.0f);
        const auto move = juce::AffineTransform::scale(zoom, zoom, kPivotX * s, kPivotY * s).translated(shake * s, lift * s);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImageTransformed(boxer_, place.followedBy(move));

        if (lunge_ > 0.01f)
        {
            // A red flash behind each glove that scales with the hit.
            for (auto pos : {juce::Point<float>(780.0f, 255.0f), juce::Point<float>(490.0f, 205.0f)})
            {
                const auto centre = juce::Point<float>(pos.x * s, pos.y * s).transformedBy(move);
                juce::ColourGradient glow(kRed.withAlpha(0.30f * lunge_), centre, kRed.withAlpha(0.0f),
                                          centre.translated(150.0f * s, 0.0f), true);
                g.setGradientFill(glow);
                g.fillEllipse(centre.x - 150.0f * s, centre.y - 150.0f * s, 300.0f * s, 300.0f * s);
            }
        }
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("PUNCH", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("TRANSIENT STACK", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void PunchAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](PunchKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(freq_, 60.0f, 110.0f);
    place(punch_, 60.0f, 370.0f);
    place(rounds_, 980.0f, 110.0f);
    place(power_, 980.0f, 370.0f);
}
