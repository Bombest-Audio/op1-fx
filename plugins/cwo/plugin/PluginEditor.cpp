#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kViolet(0xffa77bff);
const juce::Colour kRed(0xffff4d5e);
const juce::Colour kMint(0xff2ef0a0);
const juce::Colour kTrack(0xff23262a);
const juce::Colour kMuted(0xff6c7370);
const juce::Colour kInk(0xffe9ece8);
const juce::Colour kPlate(0xff07080c);
const juce::Colour kCyan(0xff4fd8ff);

juce::Font condensed(float height, bool bold)
{
    juce::Font f(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    f.setHorizontalScale(0.82f);
    return f;
}

// The halo sits behind the cow. design/make_faceplate.py uses the same centre and radii for the dim static ring
// it bakes under the transparent part of the faceplate.
constexpr float kHaloX = 506.0f, kHaloY = 214.0f, kHaloR0 = 150.0f, kHaloR1 = 190.0f;
constexpr int kTicks = 48;
constexpr float kEyeX = 429.0f, kEyeY = 162.0f; // the cow's eye in faceplate pixels
constexpr int kTimerHz = 60;
// The core reports about 0.4 for a quiet echo and up to 1.0 and over for a hot one.
constexpr float kFullActivity = 0.8f;
} // namespace

CwoKnob::CwoKnob(juce::String label, juce::String caption, juce::Colour tone,
                 std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void CwoKnob::paint(juce::Graphics& g)
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

CwoAudioProcessorEditor::CwoAudioProcessorEditor(CwoAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      freq_("FREQ", "SHIFT HZ", kBlue,
            [](double v) {
                const double hz = cwo_shift_hz(static_cast<float>(v));
                if (hz >= 1000.0)
                    return juce::String(hz / 1000.0, 1) + "k";
                return hz < 10.0 ? juce::String(hz, 1) : juce::String(juce::roundToInt(hz));
            }),
      delay_("DELAY", "ECHO MS", kViolet,
             [](double v) { return juce::String(juce::roundToInt(cwo_delay_ms(static_cast<float>(v)))); }),
      fdback_("FDBACK", "REPEATS", kRed, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      sideband_("SIDEBAND", "LOWER TO UPPER", kMint, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&freq_, &delay_, &fdback_, &sideband_})
    {
        addAndMakeVisible(*k);
        k->setRange(0.0, 1.0);
    }

    freq_.setDoubleClickReturnValue(true, CWO_DEFAULT_FREQ);
    delay_.setDoubleClickReturnValue(true, CWO_DEFAULT_DELAY);
    fdback_.setDoubleClickReturnValue(true, CWO_DEFAULT_FDBACK);
    sideband_.setDoubleClickReturnValue(true, CWO_DEFAULT_SIDEBAND);

    freqA_ = std::make_unique<Attachment>(processor_.apvts, "freq", freq_);
    delayA_ = std::make_unique<Attachment>(processor_.apvts, "delay", delay_);
    fdbackA_ = std::make_unique<Attachment>(processor_.apvts, "fdback", fdback_);
    sidebandA_ = std::make_unique<Attachment>(processor_.apvts, "sideband", sideband_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

CwoAudioProcessorEditor::~CwoAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> CwoAudioProcessorEditor::haloRegion()
{
    return {kHaloX - kHaloR1 - 12.0f, kHaloY - kHaloR1 - 12.0f, 2.0f * (kHaloR1 + 12.0f), 2.0f * (kHaloR1 + 12.0f)};
}

void CwoAudioProcessorEditor::showLevel(float level, float turns)
{
    previewHold_ = true;
    level_ = juce::jlimit(0.0f, 1.0f, level);
    angle_ = static_cast<double>(turns) * juce::MathConstants<double>::twoPi;
    repaint();
}

void CwoAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const float hit = juce::jlimit(0.0f, 1.0f, processor_.takeActivity() / kFullActivity);
        // Rise within a frame or two, fall exponentially: 0.95 per frame is about 330 ms to 1/e.
        level_ = hit > level_ ? level_ + 0.5f * (hit - level_) : level_ * 0.95f;
        if (level_ < 0.002f)
            level_ = 0.0f;

        // Spin follows the shifter: faster for a bigger shift, clockwise for the upper sideband, counter-clockwise
        // for the lower one, still at the ring modulation setting. Capped at a third of a turn per second.
        const float f = static_cast<float>(freq_.getValue());
        const float s = static_cast<float>(sideband_.getValue());
        const float dir = 2.0f * s - 1.0f;
        const float turnsPerSecond = 0.04f + 0.30f * f;
        angle_ += static_cast<double>(dir * turnsPerSecond) * juce::MathConstants<double>::twoPi / kTimerHz;
        phase_ += juce::MathConstants<double>::twoPi / (3.1 * kTimerHz);
    }
    repaint(haloRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                .getSmallestIntegerContainer());
}

void CwoAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    const float s = static_cast<float>(getWidth()) / kDesignW;

    {
        // Live halo, behind the cow. The faceplate image is transparent everywhere except the cow and the floor line.
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::scale(s));
        const juce::Colour tones[4] = {kViolet, kMint, kRed, kBlue};
        const float lvl = level_;
        for (int i = 0; i < kTicks; ++i)
        {
            const float a = static_cast<float>(angle_) + juce::MathConstants<float>::twoPi * static_cast<float>(i) / kTicks;
            const float shimmer = 0.5f + 0.5f * std::sin(static_cast<float>(phase_) * 2.0f + static_cast<float>(i) * 0.7f);
            const float alpha = 0.16f + lvl * (0.30f + 0.40f * shimmer);
            const float r1 = kHaloR1 + 7.0f * lvl * shimmer;
            const float cs = std::cos(a), sn = std::sin(a);
            g.setColour(tones[i % 4].withAlpha(alpha));
            g.drawLine(kHaloX + cs * kHaloR0, kHaloY + sn * kHaloR0, kHaloX + cs * r1, kHaloY + sn * r1, 3.0f);
        }
    }

    if (background_.isValid())
    {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }

    if (level_ > 0.01f)
    {
        // The cow's eye brightens with the signal.
        const auto c = juce::Point<float>(kEyeX * s, kEyeY * s);
        const float rad = 16.0f * s;
        juce::ColourGradient glow(kCyan.withAlpha(0.55f * level_), c, kCyan.withAlpha(0.0f), c.translated(rad, 0.0f), true);
        g.setGradientFill(glow);
        g.fillEllipse(c.x - rad, c.y - rad, 2.0f * rad, 2.0f * rad);
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("CWO", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("FREQUENCY SHIFT DELAY", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void CwoAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](CwoKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(freq_, 60.0f, 110.0f);
    place(delay_, 60.0f, 370.0f);
    place(fdback_, 980.0f, 110.0f);
    place(sideband_, 980.0f, 370.0f);
}
