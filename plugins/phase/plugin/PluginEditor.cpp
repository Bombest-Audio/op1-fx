#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;
constexpr float kMiniW = 100.0f;
constexpr float kMiniH = 120.0f;

// The four colours are the ones the OP-1 screen uses for its four knobs.
const juce::Colour kBlue(0xff698eff);
const juce::Colour kGreen(0xff00ed95);
const juce::Colour kWhite(0xffdfd9ff);
const juce::Colour kRed(0xffff3a5d);
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

constexpr int kTimerHz = 60;
// The scope panel inside the 1280x630 faceplate (design units).
constexpr float kScopeX = 350.0f, kScopeY = 100.0f, kScopeW = 580.0f, kScopeH = 250.0f;

juce::String percent(double v) { return juce::String(juce::roundToInt(v * 100.0)); }
juce::String timeShort(double s)
{
    if (s < 0.995)
        return juce::String(juce::roundToInt(s * 1000.0)) + "ms";
    return juce::String(s, s < 9.95 ? 1 : 0) + "s";
}
} // namespace

PhaseKnob::PhaseKnob(juce::String label, juce::String caption, juce::Colour tone,
                     std::function<juce::String(double)> format, bool mini)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format)), mini_(mini)
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(mini ? 160 : 220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void PhaseKnob::paint(juce::Graphics& g)
{
    const float cw = mini_ ? kMiniW : kKnobW, ch = mini_ ? kMiniH : kKnobH;
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / cw, static_cast<float>(getHeight()) / ch));

    const float a0 = juce::degreesToRadians(-135.0f), a1 = juce::degreesToRadians(135.0f);
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));
    const float aV = a0 + (a1 - a0) * prop;

    const float cx = cw * 0.5f;
    const float cy = mini_ ? 62.0f : 130.0f;
    const float r = mini_ ? 30.0f : 74.0f;
    const float stroke = mini_ ? 3.4f : 6.2f;

    g.setColour(tone_);
    if (mini_)
    {
        g.setFont(condensed(17.0f, true).withExtraKerningFactor(0.2f));
        g.drawText(label_, 0, 0, static_cast<int>(cw), 20, juce::Justification::centred);
    }
    else
    {
        g.setFont(condensed(46.0f, true).withExtraKerningFactor(0.1f));
        g.drawText(label_, 0, 0, static_cast<int>(cw), 40, juce::Justification::centred);
    }

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
    const float dot = mini_ ? 3.6f : 7.0f;
    g.setColour(tone_);
    g.fillEllipse(tx - dot, ty - dot, dot * 2.0f, dot * 2.0f);

    g.setColour(kInk);
    if (mini_)
    {
        g.setFont(condensed(22.0f, false));
        g.drawText(format_(getValue()), 0, 45, static_cast<int>(cw), 40, juce::Justification::centred);
        g.setColour(kMuted);
        g.setFont(juce::Font(juce::FontOptions(10.0f)).withExtraKerningFactor(0.16f));
        g.drawText(caption_, 0, 98, static_cast<int>(cw), 14, juce::Justification::centred);
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

PhaseAudioProcessorEditor::PhaseAudioProcessorEditor(PhaseAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      a_("A", "POSITIVE BEND", kBlue, percent, false),
      b_("B", "NEGATIVE BEND", kGreen, percent, false),
      r_("R", "RESONANCE", kWhite, percent, false),
      w_("W", "WINDOW", kRed, percent, false),
      attack_("ATK", "", kInk, timeShort, true),
      decay_("DEC", "", kInk, timeShort, true),
      sustain_("SUS", "PERCENT", kInk, percent, true),
      release_("REL", "", kInk, timeShort, true),
      level_("LEVEL", "PERCENT", kInk, percent, true),
      bend_("BEND", "SEMITONES", kInk, [](double v) { return juce::String(juce::roundToInt(v)); }, true)
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&a_, &b_, &r_, &w_, &attack_, &decay_, &sustain_, &release_, &level_, &bend_})
        addAndMakeVisible(*k);

    a_.setRange(0.0, 1.0);
    b_.setRange(0.0, 1.0);
    r_.setRange(0.0, 1.0);
    w_.setRange(0.0, 1.0);
    sustain_.setRange(0.0, 1.0);
    level_.setRange(0.0, 1.0);
    bend_.setRange(0.0, 12.0, 1.0);

    a_.setDoubleClickReturnValue(true, PHASE_DEFAULT_A);
    b_.setDoubleClickReturnValue(true, PHASE_DEFAULT_B);
    r_.setDoubleClickReturnValue(true, PHASE_DEFAULT_R);
    w_.setDoubleClickReturnValue(true, PHASE_DEFAULT_W);
    sustain_.setDoubleClickReturnValue(true, PHASE_DEFAULT_SUSTAIN);
    level_.setDoubleClickReturnValue(true, PHASE_DEFAULT_LEVEL);
    bend_.setDoubleClickReturnValue(true, PHASE_DEFAULT_BEND_RANGE);

    // The time knobs take their range (and skew) from the parameters through the attachments below.
    aA_ = std::make_unique<Attachment>(processor_.apvts, "a", a_);
    bA_ = std::make_unique<Attachment>(processor_.apvts, "b", b_);
    rA_ = std::make_unique<Attachment>(processor_.apvts, "r", r_);
    wA_ = std::make_unique<Attachment>(processor_.apvts, "w", w_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    bendA_ = std::make_unique<Attachment>(processor_.apvts, "bend", bend_);

    attack_.setDoubleClickReturnValue(true, PHASE_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, PHASE_DEFAULT_DECAY);
    release_.setDoubleClickReturnValue(true, PHASE_DEFAULT_RELEASE);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

PhaseAudioProcessorEditor::~PhaseAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> PhaseAudioProcessorEditor::scopeRegion()
{
    return {kScopeX, kScopeY, kScopeW, kScopeH};
}

void PhaseAudioProcessorEditor::showDemo(float level, int voices)
{
    demoHold_ = true;
    glow_ = juce::jlimit(0.0f, 1.0f, level);
    voices_ = juce::jlimit(0, PHASE_VOICES, voices);
    sweep_ = 0.31;
    repaint();
}

void PhaseAudioProcessorEditor::timerCallback()
{
    if (!demoHold_)
    {
        const float peak = juce::jlimit(0.0f, 1.0f, processor_.takeLevel() / 0.5f);
        // Rise quickly, fall over about 250 ms (0.93 per frame at 60 Hz).
        glow_ = peak > glow_ ? glow_ + 0.5f * (peak - glow_) : glow_ * 0.93f;
        if (glow_ < 0.003f)
            glow_ = 0.0f;
        voices_ = processor_.voiceCount();
        // The marker crosses the cycle once every 2.4 s at rest and a little faster when it is loud. This is a visual
        // device only: the real phase runs hundreds of times a second and cannot be shown at the timer rate.
        sweep_ += (1.0 / (2.4 * kTimerHz)) * (1.0 + 1.5 * glow_);
        sweep_ -= std::floor(sweep_);
    }
    repaint(scopeRegion().expanded(8.0f).transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW)).getSmallestIntegerContainer());
}

void PhaseAudioProcessorEditor::paintScope(juce::Graphics& g)
{
    // Called with the graphics context already scaled to the 1280x630 design space.
    const auto box = scopeRegion();
    g.setColour(kPlate.withAlpha(0.62f));
    g.fillRoundedRectangle(box, 10.0f);
    g.setColour(kTrack);
    g.drawRoundedRectangle(box.reduced(0.5f), 10.0f, 1.0f);

    const float pa = static_cast<float>(a_.getValue());
    const float pb = static_cast<float>(b_.getValue());
    const float pr = static_cast<float>(r_.getValue());
    const float pw = static_cast<float>(w_.getValue());

    const auto inner = box.reduced(34.0f, 30.0f);
    const float midY = inner.getCentreY();

    // axes in the style of the OP-1 screen: a vertical axis with an arrowhead and a baseline
    g.setColour(juce::Colour(0xff656579));
    g.drawLine(inner.getX(), inner.getBottom(), inner.getX(), inner.getY() - 6.0f, 1.5f);
    g.drawLine(inner.getX(), midY, inner.getRight() + 6.0f, midY, 1.5f);
    juce::Path arrow;
    arrow.addTriangle(inner.getX() - 4.0f, inner.getY() - 2.0f, inner.getX(), inner.getY() - 10.0f, inner.getX() + 4.0f, inner.getY() - 2.0f);
    arrow.addTriangle(inner.getRight() + 2.0f, midY - 4.0f, inner.getRight() + 10.0f, midY, inner.getRight() + 2.0f, midY + 4.0f);
    g.setColour(juce::Colour(0xff7c7990));
    g.fillPath(arrow);

    constexpr int kPoints = 240;
    float ys[kPoints + 1];
    float peak = 0.2f;
    for (int i = 0; i <= kPoints; ++i)
    {
        ys[i] = phase_shape(static_cast<float>(i) / kPoints, pa, pb, pr, pw);
        peak = juce::jmax(peak, std::abs(ys[i]));
    }
    const float amp = inner.getHeight() * 0.5f * 0.92f / peak;
    auto px = [&](int i) { return inner.getX() + inner.getWidth() * static_cast<float>(i) / kPoints; };
    auto py = [&](int i) { return midY - ys[i] * amp; };

    auto strokeRange = [&](int from, int to, juce::Colour c) {
        juce::Path path;
        path.startNewSubPath(px(from), py(from));
        for (int i = from + 1; i <= to; ++i)
            path.lineTo(px(i), py(i));
        const float lift = 0.10f + 0.22f * glow_;
        g.setColour(c.withAlpha(lift));
        g.strokePath(path, juce::PathStrokeType(9.0f + 5.0f * glow_, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(c);
        g.strokePath(path, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    strokeRange(0, kPoints / 2, kBlue);
    strokeRange(kPoints / 2, kPoints, kGreen);

    // end dots like the OP-1 curve
    g.setColour(kBlue);
    g.fillEllipse(px(0) - 4.0f, py(0) - 4.0f, 8.0f, 8.0f);
    g.setColour(kGreen);
    g.fillEllipse(px(kPoints) - 4.0f, py(kPoints) - 4.0f, 8.0f, 8.0f);

    // the marker rides the wave while notes sound
    if (glow_ > 0.01f)
    {
        const float x = static_cast<float>(sweep_) * static_cast<float>(kPoints);
        const int i0 = juce::jlimit(0, kPoints - 1, static_cast<int>(x));
        const float f = x - static_cast<float>(i0);
        const float mx = px(i0) + (px(i0 + 1) - px(i0)) * f;
        const float my = py(i0) + (py(i0 + 1) - py(i0)) * f;
        juce::ColourGradient halo(kWhite.withAlpha(0.55f * glow_), mx, my, kWhite.withAlpha(0.0f), mx + 26.0f, my, true);
        g.setGradientFill(halo);
        g.fillEllipse(mx - 26.0f, my - 26.0f, 52.0f, 52.0f);
        g.setColour(kWhite.withAlpha(0.35f + 0.65f * glow_));
        g.fillEllipse(mx - 5.0f, my - 5.0f, 10.0f, 10.0f);
    }

    // eight voice pips under the axis
    const float pipY = box.getBottom() - 14.0f;
    for (int i = 0; i < PHASE_VOICES; ++i)
    {
        const float x = box.getCentreX() - 4.0f * 18.0f + 9.0f + 18.0f * static_cast<float>(i);
        g.setColour(i < voices_ ? kWhite : kTrack);
        g.fillEllipse(x - 3.0f, pipY - 3.0f, 6.0f, 6.0f);
    }
}

void PhaseAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    paintScope(g);

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("PHASE", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("PHASE DISTORTION SYNTH", 50, 62, 300, 16, juce::Justification::centredLeft);

    // the envelope strip label
    g.setColour(kMuted);
    g.drawText("AMP ENVELOPE", static_cast<int>(kScopeX), 372, 200, 14, juce::Justification::centredLeft);
}

void PhaseAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](PhaseKnob& k, float x, float y, float w, float h) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(a_, 60.0f, 110.0f, kKnobW, kKnobH);
    place(b_, 60.0f, 370.0f, kKnobW, kKnobH);
    place(r_, 980.0f, 110.0f, kKnobW, kKnobH);
    place(w_, 980.0f, 370.0f, kKnobW, kKnobH);

    const float x0 = kScopeX + (kScopeW - 6.0f * kMiniW - 5.0f * 6.0f) * 0.5f;
    PhaseKnob* minis[6] = {&attack_, &decay_, &sustain_, &release_, &level_, &bend_};
    for (int i = 0; i < 6; ++i)
        place(*minis[i], x0 + static_cast<float>(i) * (kMiniW + 6.0f), 400.0f, kMiniW, kMiniH);
}
