#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr float kDesignW = 1280.0f;
constexpr float kDesignH = 630.0f;
constexpr float kKnobW = 240.0f;
constexpr float kKnobH = 250.0f;

const juce::Colour kBlue(0xff698eff);
const juce::Colour kMint(0xff2ef0a0);
const juce::Colour kLavender(0xffcdb8ff);
const juce::Colour kOrange(0xffff8a4d);
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

// The two springs in the faceplate art, in design pixels: left end to right end of each coil.
struct Axis { juce::Point<float> a, b; };
constexpr Axis kSprings[2] = {{{402.0f, 318.0f}, {900.0f, 236.0f}}, {{428.0f, 386.0f}, {930.0f, 296.0f}}};
constexpr float kRegionX = 330.0f, kRegionY = 130.0f, kRegionW = 640.0f, kRegionH = 380.0f;
constexpr int kTimerHz = 60;
// The tank output peaks around 0.2 to 0.5 for a normal signal at the factory settings. 0.35 and up reads as full.
constexpr float kFullActivity = 0.35f;
} // namespace

SpringKnob::SpringKnob(juce::String label, juce::String caption, juce::Colour tone,
                       std::function<juce::String(double)> format)
    : label_(std::move(label)), caption_(std::move(caption)), tone_(tone), format_(std::move(format))
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(220);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void SpringKnob::paint(juce::Graphics& g)
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

SpringAudioProcessorEditor::SpringAudioProcessorEditor(SpringAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      tone_("TONE", "LOOP LOW-PASS HZ", kBlue,
            [](double v) {
                const double hz = spring_tone_hz(static_cast<float>(v));
                return hz >= 1000.0 ? juce::String(hz / 1000.0, 1) + "k" : juce::String(juce::roundToInt(hz));
            }),
      turns_("TURNS", "CHIRP STRENGTH", kMint, [](double v) { return juce::String(juce::roundToInt(v * 100.0)); }),
      damp_("DAMP", "BASS DECAY SECONDS", kLavender,
            [](double v) { return juce::String(spring_rt60(static_cast<float>(v)), 1); }),
      level_("LEVEL", "DRIVE INTO THE SPRINGS", kOrange,
             [](double v) { return juce::String(juce::roundToInt(v * 100.0)); })
{
    background_ = juce::ImageCache::getFromMemory(BinaryData::faceplatebg1280x630_png,
                                                  BinaryData::faceplatebg1280x630_pngSize);

    for (auto* k : {&tone_, &turns_, &damp_, &level_})
    {
        addAndMakeVisible(*k);
        k->setRange(0.0, 1.0);
    }

    tone_.setDoubleClickReturnValue(true, SPRING_DEFAULT_TONE);
    turns_.setDoubleClickReturnValue(true, SPRING_DEFAULT_TURNS);
    damp_.setDoubleClickReturnValue(true, SPRING_DEFAULT_DAMP);
    level_.setDoubleClickReturnValue(true, SPRING_DEFAULT_LEVEL);

    toneA_ = std::make_unique<Attachment>(processor_.apvts, "tone", tone_);
    turnsA_ = std::make_unique<Attachment>(processor_.apvts, "turns", turns_);
    dampA_ = std::make_unique<Attachment>(processor_.apvts, "damp", damp_);
    levelA_ = std::make_unique<Attachment>(processor_.apvts, "level", level_);

    constrainer_.setFixedAspectRatio(static_cast<double>(kDesignW / kDesignH));
    constrainer_.setSizeLimits(640, 315, 1920, 945);
    setConstrainer(&constrainer_);
    setResizable(true, true);
    setSize(960, 473);

    startTimerHz(kTimerHz);
}

SpringAudioProcessorEditor::~SpringAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> SpringAudioProcessorEditor::springRegion()
{
    return {kRegionX, kRegionY, kRegionW, kRegionH};
}

void SpringAudioProcessorEditor::showEnergy(float energy, float pulsePosition)
{
    previewHold_ = true;
    energy_ = juce::jlimit(0.0f, 1.0f, energy);
    pulse_ = juce::jlimit(0.0f, 1.0f, pulsePosition);
    repaint();
}

void SpringAudioProcessorEditor::timerCallback()
{
    if (!previewHold_)
    {
        // The tank output is quiet for most material, so lift it with a square root before it drives the glow.
        const float hit = std::sqrt(juce::jlimit(0.0f, 1.0f, processor_.takeActivity() / kFullActivity));
        // Rise at once, fall exponentially: 0.95 per frame is about 330 ms to 1/e.
        energy_ = hit > energy_ ? hit : energy_ * 0.95f;
        if (energy_ < 0.003f)
            energy_ = 0.0f;
        // The glow travels the length of the springs in about 0.8 s, faster when the tank is excited.
        pulse_ += (0.9f + 0.9f * energy_) / static_cast<float>(kTimerHz);
        pulse_ -= std::floor(pulse_);
    }
    const bool animating = energy_ > 0.0f;
    if (animating || previewHold_)
        repaint(springRegion().transformedBy(juce::AffineTransform::scale(static_cast<float>(getWidth()) / kDesignW))
                    .getSmallestIntegerContainer());
}

void SpringAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kPlate);
    if (background_.isValid())
        g.drawImage(background_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    const float s = static_cast<float>(getWidth()) / kDesignW;

    if (energy_ > 0.004f)
    {
        // One soft glow runs down each spring and a fainter one comes back the other way, like a wave
        // reflecting off the far end. Alpha stays low so the art still reads as a photograph.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(springRegion().transformedBy(juce::AffineTransform::scale(s)).toNearestInt());
        for (const auto& sp : kSprings)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                const float t = pass == 0 ? pulse_ : 1.0f - pulse_;
                const float alpha = (pass == 0 ? 0.34f : 0.14f) * energy_;
                const auto c = juce::Point<float>(sp.a.x + (sp.b.x - sp.a.x) * t, sp.a.y + (sp.b.y - sp.a.y) * t) * s;
                const float rx = 70.0f * s, ry = 20.0f * s;
                juce::ColourGradient glow(kMint.withAlpha(alpha), c, kMint.withAlpha(0.0f), c.translated(rx, 0.0f), true);
                g.setGradientFill(glow);
                // The gradient is circular. Squash it to the spring's width with a transform.
                const auto squash = juce::AffineTransform::scale(1.0f, ry / rx, c.x, c.y);
                g.addTransform(squash);
                g.fillEllipse(c.x - rx, c.y - rx, 2.0f * rx, 2.0f * rx);
                g.addTransform(squash.inverted());
            }
        }
    }

    g.addTransform(juce::AffineTransform::scale(s));

    g.setColour(kInk);
    g.setFont(condensed(40.0f, true).withExtraKerningFactor(0.34f));
    g.drawText("SPRING", 48, 24, 300, 40, juce::Justification::centredLeft);
    g.setColour(kMuted);
    g.setFont(juce::Font(juce::FontOptions(12.0f)).withExtraKerningFactor(0.16f));
    g.drawText("DISPERSIVE TANK", 50, 62, 300, 16, juce::Justification::centredLeft);
}

void SpringAudioProcessorEditor::resized()
{
    const float s = static_cast<float>(getWidth()) / kDesignW;
    auto place = [s](SpringKnob& k, float x, float y) {
        k.setBounds(juce::Rectangle<float>(x * s, y * s, kKnobW * s, kKnobH * s).toNearestInt());
    };
    place(tone_, 60.0f, 110.0f);
    place(turns_, 60.0f, 370.0f);
    place(damp_, 980.0f, 110.0f);
    place(level_, 980.0f, 370.0f);
}
