#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

// Knob colors are the OP-1 screen colors for the four Grid parameters.
const juce::Colour kBlue(0xff698eff);     // X
const juce::Colour kGreen(0xff00ed95);    // Y
const juce::Colour kLavender(0xffadadff); // Z FDB
const juce::Colour kRed(0xffff3a5d);      // MIX
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

// The lattice art sits in a 620 px wide panel in the middle of the faceplate. The ripple origin is where the
// artwork's central bump is, measured from faceplate-bg-1280x630.png.
constexpr float kLatticeX = 330.0f, kLatticeW = 620.0f;
constexpr float kCentreX = 640.0f, kCentreY = 318.0f;
constexpr int kTimerHz = 60;
constexpr float kRippleSeconds = 1.2f;
constexpr float kRippleReach = 250.0f;     // horizontal radius at the end of a ripple, in design pixels
// Wet peak of about 0.3 is a normal hit and 0.5 and up reads as a full one.
constexpr float kFullActivity = 0.5f;
constexpr float kSpawnThreshold = 0.06f;   // glow level that starts a ripple
constexpr float kSpawnGap = 0.14f;         // seconds between ripples
} // namespace

GridKnob::GridKnob(juce::String label, juce::String caption, juce::Colour tone,
                   std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void GridKnob::paint(juce::Graphics& g)
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

GridAudioProcessorEditor::GridAudioProcessorEditor(GridAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      x_("X", "MESH WIDTH, CELLS", kBlue,
         [](double v) { return juce::String(grid_cells_from_norm(static_cast<float>(v))); }),
      y_("Y", "MESH HEIGHT, CELLS", kGreen,
         [](double v) { return juce::String(grid_cells_from_norm(static_cast<float>(v))); }),
      zfb_("Z FDB", "FEEDBACK PER PASS", kLavender, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      mix_("MIX", "SEND INTO THE MESH", kRed, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&x_, &y_, &zfb_, &mix_})
    {
        addAndMakeVisible(*k);
        k->setRange(0.0, 1.0);
    }

    x_.setDoubleClickReturnValue(true, GRID_DEFAULT_X);
    y_.setDoubleClickReturnValue(true, GRID_DEFAULT_Y);
    zfb_.setDoubleClickReturnValue(true, GRID_DEFAULT_ZFB);
    mix_.setDoubleClickReturnValue(true, GRID_DEFAULT_MIX);

    xA_ = std::make_unique<Attachment>(processor_.apvts, "x", x_);
    yA_ = std::make_unique<Attachment>(processor_.apvts, "y", y_);
    zfbA_ = std::make_unique<Attachment>(processor_.apvts, "zfb", zfb_);
    mixA_ = std::make_unique<Attachment>(processor_.apvts, "mix", mix_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    pass_ = passText();
    startTimerHz(kTimerHz);
}

GridAudioProcessorEditor::~GridAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> GridAudioProcessorEditor::latticeRegion()
{
    return {kLatticeX, 0.0f, kLatticeW, kDesignH};
}

juce::String GridAudioProcessorEditor::passText() const
{
    const int nx = grid_cells_from_norm(*processor_.apvts.getRawParameterValue("x"));
    const int ny = grid_cells_from_norm(*processor_.apvts.getRawParameterValue("y"));
    return "PASS " + juce::String(juce::roundToInt(grid_pass_ms(nx, ny))) + " MS";
}

void GridAudioProcessorEditor::spawnRipple(float strength)
{
    // Reuse the oldest slot, which is the one that is furthest along or already finished.
    auto* oldest = &ripples_[0];
    for (auto& r : ripples_)
        if (r.age > oldest->age)
            oldest = &r;
    oldest->age = 0.0f;
    oldest->strength = strength;
    sinceSpawn_ = 0.0f;
}

void GridAudioProcessorEditor::showHit(float strength)
{
    previewHold_ = true;
    glow_ = juce::jlimit(0.0f, 1.0f, strength);
    ripples_[0] = {0.20f, glow_};
    ripples_[1] = {0.55f, glow_ * 0.8f};
    repaint();
}

void GridAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        const float dt = 1.0f / static_cast<float>(kTimerHz);
        const float hit = juce::jlimit(0.0f, 1.0f, processor_.takeActivity() / kFullActivity);
        // Rise instantly with the signal. Fall exponentially: 0.92 per frame is about 200 ms to 1/e.
        glow_ = hit > glow_ ? hit : glow_ * 0.92f;
        if (glow_ < 0.002f)
            glow_ = 0.0f;

        sinceSpawn_ += dt;
        if (hit > kSpawnThreshold && sinceSpawn_ > kSpawnGap)
            spawnRipple(hit);

        for (auto& r : ripples_)
            if (r.age < kRippleSeconds)
                r.age += dt;
    }

    const auto text = passText();
    if (text != pass_)
    {
        pass_ = text;
        repaint();
    }
    else
    {
        const float s = static_cast<float>(getWidth()) / kDesignW;
        repaint(latticeRegion().transformedBy(juce::AffineTransform::scale(s)).getSmallestIntegerContainer());
    }
}

void GridAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    {
        // Signal-driven overlay on the lattice: a soft glow at the centre that follows the wet level, and
        // isometric ripples (2:1 ellipses) that leave the centre with each new hit. Alpha stays at or under 0.35.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(latticeRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        g.addTransform(juce::AffineTransform::scale(s));

        if (glow_ > 0.01f)
        {
            juce::ColourGradient glow(kGreen.withAlpha(0.22f * glow_), kCentreX, kCentreY, kGreen.withAlpha(0.0f),
                                      kCentreX + 150.0f, kCentreY, true);
            g.setGradientFill(glow);
            g.fillEllipse(kCentreX - 150.0f, kCentreY - 75.0f, 300.0f, 150.0f);
        }

        for (const auto& r : ripples_)
        {
            if (r.age >= kRippleSeconds)
                continue;
            const float t = r.age / kRippleSeconds;
            const float ease = 1.0f - (1.0f - t) * (1.0f - t); // fast out, slow in
            const float rx = 24.0f + kRippleReach * ease;
            const float fade = (1.0f - t) * (1.0f - t);
            g.setColour(kGreen.withAlpha(0.35f * r.strength * fade));
            g.drawEllipse(kCentreX - rx, kCentreY - rx * 0.5f, rx * 2.0f, rx, 2.0f);
        }
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("GRID", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("WAVEGUIDE MESH", 50, 62, 300, 16, juce::Justification::centredLeft);
    g.drawText(pass_, static_cast<int>(kDesignW) - 48 - 300, 30, 300, 16, juce::Justification::centredRight);
}

void GridAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](GridKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(x_, 60.0f, 110.0f);
    place(y_, 60.0f, 370.0f);
    place(zfb_, 980.0f, 110.0f);
    place(mix_, 980.0f, 370.0f);
}
