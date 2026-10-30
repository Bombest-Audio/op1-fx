#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;
constexpr float kMiniW = 96.0f;
constexpr float kMiniH = 64.0f;
constexpr int kTimerHz = 60;

const juce::Colour kMint(0xff2ef0a0);
const juce::Colour kViolet(0xffa77bff);
const juce::Colour kAmber(0xffffb347);
const juce::Colour kCoral(0xffff4d5e);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);

// Oscillator dots: 4 by 2 grid centred in the dark gap of the faceplate art.
constexpr float kDotCx = 640.0f, kDotCy = 325.0f, kDotStep = 22.0f;
// The strands fan out of the left end of the art; the glow sits there and follows the output level.
constexpr float kGlowX = 336.0f, kGlowY = 320.0f;

const juce::Colour kDotTones[CLUSTER_OSCS] = {kMint, kViolet, kAmber, kCoral, kMint, kViolet, kAmber, kCoral};

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

juce::Point<float> dotPos(int i)
{
    const int col = i % 4, row = i / 4;
    return {kDotCx + (static_cast<float>(col) - 1.5f) * kDotStep, kDotCy + (static_cast<float>(row) - 0.5f) * kDotStep};
}

juce::String timeLabel(double norm)
{
    const float s = cluster_env_time(static_cast<float>(norm));
    return s < 1.0f ? juce::String(juce::roundToInt(s * 1000.0f)) + "ms" : juce::String(s, 1) + "s";
}
juce::String percentLabel(double v) { return juce::String(juce::roundToInt(v * 100.0)); }
} // namespace

ClusterKnob::ClusterKnob(juce::String label, juce::String caption, juce::Colour tone,
                         std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ClusterKnob::paint(juce::Graphics& g)
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

ClusterMini::ClusterMini(juce::String label, juce::Colour tone, std::function<juce::String(double)> format)
    : label_(std::move(label)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::LinearBarVertical); // vertical drag, no on-screen text; painted by hand below
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(160);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ClusterMini::paint(juce::Graphics& g)
{
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kMiniW,
                                                static_cast<float>(getHeight()) / kMiniH));
    const auto prop = static_cast<float>(valueToProportionOfLength(getValue()));

    g.setColour(kMuted);
    g.setFont(condensed(18.0f, true).withExtraKerningFactor(0.2f));
    g.drawText(label_, 0, 0, static_cast<int>(kMiniW), 18, juce::Justification::centred);

    g.setColour(kInk);
    g.setFont(condensed(26.0f, false));
    g.drawText(format_(getValue()), 0, 18, static_cast<int>(kMiniW), 28, juce::Justification::centred);

    const float x0 = 10.0f, w = kMiniW - 20.0f, y = 54.0f;
    g.setColour(kTrack);
    g.fillRoundedRectangle(x0, y - 2.0f, w, 4.0f, 2.0f);
    g.setColour(tone_);
    g.fillRoundedRectangle(x0, y - 2.0f, w * prop, 4.0f, 2.0f);
    g.fillEllipse(x0 + w * prop - 4.5f, y - 4.5f, 9.0f, 9.0f);
}

ClusterAudioProcessorEditor::ClusterAudioProcessorEditor(ClusterAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      oscs_("OSCS", "OSCILLATORS IN THE CLUSTER", kMint,
            [](double v) { return juce::String(juce::roundToInt(v)); }),
      feedback_("FEEDBACK", "SELF PHASE MODULATION", kViolet, percentLabel),
      spread_("SPREAD", "DETUNE SPREAD AND DECAY", kAmber, percentLabel),
      wander_("WANDER", "RANDOM KICK SIZE", kCoral,
              [](double v) { return juce::String(v * 100.0, 1); }),
      attack_("ATK", kMint, timeLabel),
      decay_("DEC", kViolet, timeLabel),
      sustain_("SUS", kAmber, percentLabel),
      release_("REL", kCoral, timeLabel),
      level_("LEVEL", kInk, percentLabel),
      bend_("BEND", kMuted, [](double v) { return juce::String(juce::roundToInt(v)) + "st"; })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {static_cast<juce::Slider*>(&oscs_), static_cast<juce::Slider*>(&feedback_),
                    static_cast<juce::Slider*>(&spread_), static_cast<juce::Slider*>(&wander_),
                    static_cast<juce::Slider*>(&attack_), static_cast<juce::Slider*>(&decay_),
                    static_cast<juce::Slider*>(&sustain_), static_cast<juce::Slider*>(&release_),
                    static_cast<juce::Slider*>(&level_), static_cast<juce::Slider*>(&bend_)})
        addAndMakeVisible(*k);

    oscs_.setRange(1.0, static_cast<double>(CLUSTER_OSCS), 1.0);
    bend_.setRange(0.0, 12.0, 1.0);
    for (auto* k : {static_cast<juce::Slider*>(&feedback_), static_cast<juce::Slider*>(&spread_),
                    static_cast<juce::Slider*>(&wander_), static_cast<juce::Slider*>(&attack_),
                    static_cast<juce::Slider*>(&decay_), static_cast<juce::Slider*>(&sustain_),
                    static_cast<juce::Slider*>(&release_), static_cast<juce::Slider*>(&level_)})
        k->setRange(0.0, 1.0);

    oscs_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_OSCS);
    feedback_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_FEEDBACK);
    spread_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_SPREAD);
    wander_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_WANDER);
    attack_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_ATTACK);
    decay_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_DECAY);
    sustain_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_SUSTAIN);
    release_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_RELEASE);
    level_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_LEVEL);
    bend_.setDoubleClickReturnValue(true, CLUSTER_DEFAULT_BEND);

    oscsA_ = std::make_unique<Attachment>(processor_.apvts, "oscs", oscs_);
    feedbackA_ = std::make_unique<Attachment>(processor_.apvts, "feedback", feedback_);
    spreadA_ = std::make_unique<Attachment>(processor_.apvts, "spread", spread_);
    wanderA_ = std::make_unique<Attachment>(processor_.apvts, "wander", wander_);
    attackA_ = std::make_unique<Attachment>(processor_.apvts, "attack", attack_);
    decayA_ = std::make_unique<Attachment>(processor_.apvts, "decay", decay_);
    sustainA_ = std::make_unique<Attachment>(processor_.apvts, "sustain", sustain_);
    releaseA_ = std::make_unique<Attachment>(processor_.apvts, "release", release_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);
    bendA_ = std::make_unique<Attachment>(processor_.apvts, "bend", bend_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

ClusterAudioProcessorEditor::~ClusterAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> ClusterAudioProcessorEditor::dotRegion()
{
    return {kGlowX - 60.0f, 90.0f, 760.0f, 440.0f};
}

void ClusterAudioProcessorEditor::showActivity(const std::array<float, CLUSTER_OSCS>& act, float level)
{
    previewHold_ = true;
    dots_ = act;
    glow_ = juce::jlimit(0.0f, 1.0f, level);
    repaint();
}

void ClusterAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        std::array<float, CLUSTER_OSCS> act{};
        processor_.takeActivity(act);
        for (size_t i = 0; i < dots_.size(); ++i)
        {
            const float target = juce::jlimit(0.0f, 1.0f, act[i]);
            // Rise at once so a new note lights its dots on the attack; fall about 120 ms to 1/e.
            dots_[i] = target > dots_[i] ? target : dots_[i] * 0.88f;
            if (dots_[i] < 0.003f)
                dots_[i] = 0.0f;
        }
        const float lvl = juce::jlimit(0.0f, 1.0f, processor_.takeLevel());
        glow_ = lvl > glow_ ? lvl : glow_ * 0.92f;
        if (glow_ < 0.003f)
            glow_ = 0.0f;
    }
    const float s = static_cast<float>(getWidth()) / kDesignW;
    repaint(dotRegion().transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
}

void ClusterAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;
    g.addTransform(juce::AffineTransform::scale(s));

    if (glow_ > 0.01f)
    {
        // Soft bloom at the root of the strands, restrained: at most 22% alpha.
        const juce::Point<float> c(kGlowX, kGlowY);
        juce::ColourGradient glow(kInk.withAlpha(0.22f * glow_), c, kInk.withAlpha(0.0f), c.translated(170.0f, 0.0f), true);
        g.setGradientFill(glow);
        g.fillEllipse(c.x - 170.0f, c.y - 170.0f, 340.0f, 340.0f);
    }

    for (int i = 0; i < CLUSTER_OSCS; ++i)
    {
        const auto p = dotPos(i);
        const float a = dots_[static_cast<size_t>(i)];
        g.setColour(kTrack);
        g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
        if (a > 0.01f)
        {
            const auto tone = kDotTones[i];
            juce::ColourGradient halo(tone.withAlpha(0.35f * a), p, tone.withAlpha(0.0f), p.translated(16.0f, 0.0f), true);
            g.setGradientFill(halo);
            g.fillEllipse(p.x - 16.0f, p.y - 16.0f, 32.0f, 32.0f);
            g.setColour(tone.withAlpha(0.25f + 0.75f * a));
            g.fillEllipse(p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
        }
    }

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("CLUSTER", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("SELF-FEEDBACK SINE CLUSTER", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void ClusterAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](juce::Component& c, float x, float y, float w, float h) {
        c.setBounds(juce::Rectangle<float>(x * s, y * s, w * s, h * s).toNearestInt());
    };
    place(oscs_, 60.0f, 110.0f, kKnobW, kKnobH);
    place(feedback_, 60.0f, 370.0f, kKnobW, kKnobH);
    place(spread_, 980.0f, 110.0f, kKnobW, kKnobH);
    place(wander_, 980.0f, 370.0f, kKnobW, kKnobH);

    // Six small controls along the bottom of the art, 100 px pitch, centred on x = 640.
    ClusterMini* minis[] = {&attack_, &decay_, &sustain_, &release_, &level_, &bend_};
    for (int i = 0; i < 6; ++i)
        place(*minis[i], 340.0f + 100.0f * static_cast<float>(i), 545.0f, kMiniW, kMiniH);
}
