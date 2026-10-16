#include "AboutBox.h"
#include <cmath>

namespace nfui {

namespace {
const juce::Colour kLime = tok::accent;
const juce::Colour kMagenta { 0xffff2fd0 };
const juce::Colour kCyan = tok::modLfo;

juce::Font mono(float h, bool bold = true)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String versionText()
{
#ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
#else
    return "0.2";
#endif
}
} // namespace

AboutBox::AboutBox()
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, false);
    rollSerial();
    serialT0 = 0.0; // the first serial appears at once; later rolls type out
    for (int i = 0; i < 48; ++i)
        stars.push_back({ rng.nextFloat() * kW, rng.nextFloat() * 170.f, 0.3f + rng.nextFloat() * 1.7f });
}

void AboutBox::visibilityChanged()
{
    if (isVisible()) {
        t0 = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(50);
        grabKeyboardFocus();
    } else {
        stopTimer();
    }
}

void AboutBox::rollSerial()
{
    static const char* const parts[] = { "FREE", "4EVR", "AGPL", "NEUR0", "BASS", "R33S3", "WUBB", "D0PE", "SK4NK", "N0PE", "GR1ND", "T34R", "L0L", "TUIG", "FORG3", "N0NE" };
    constexpr int n = (int) (sizeof(parts) / sizeof(parts[0]));
    juce::String s;
    for (int i = 0; i < 4; ++i) s << (i ? "-" : "") << parts[rng.nextInt(n)];
    serial = s;
    serialT0 = juce::Time::getMillisecondCounterHiRes();
}

void AboutBox::close()
{
    if (onClose) onClose();
}

void AboutBox::timerCallback()
{
    for (auto& s : stars) {
        s.x -= s.v;
        if (s.x < 0.f) { s.x += (float) kW; s.y = rng.nextFloat() * 170.f; }
    }
    repaint();
}

void AboutBox::mouseMove(const juce::MouseEvent& e)
{
    const int h = genRect().contains(e.getPosition()) ? 1 : (exitRect().contains(e.getPosition()) ? 2 : (logoRect().contains(e.getPosition()) ? 3 : 0));
    if (h != hover) { hover = h; repaint(); }
}

void AboutBox::mouseExit(const juce::MouseEvent&) { if (hover) { hover = 0; repaint(); } }

juce::MouseCursor AboutBox::getMouseCursor()
{
    return hover ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor;
}

void AboutBox::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (genRect().contains(p)) rollSerial();
    else if (logoRect().contains(p)) juce::URL("https://github.com/huuba91/neuroforge").launchInDefaultBrowser();
    else if (exitRect().contains(p) || ! box().contains(p)) close();
}

bool AboutBox::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey) { rollSerial(); return true; }
    return true; // swallow everything else while open
}

void AboutBox::paint(juce::Graphics& g)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float t = (float) ((now - t0) * 0.001);

    g.fillAll(juce::Colours::black.withAlpha(0.72f));

    const auto b = box();
    g.setColour(juce::Colours::black);
    g.fillRect(b);

    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(b);
    g.addTransform(juce::AffineTransform::translation((float) b.getX(), (float) b.getY()));

    // copper / raster bars behind the scroller
    const int barTop = 214, barH = 50;
    for (int y = 0; y < barH; ++y) {
        const float u = (float) y / (float) barH;
        float v = 0.f;
        for (int k = 0; k < 3; ++k) {
            const float c = 0.5f + 0.38f * std::sin(t * (1.3f + 0.5f * (float) k) + (float) k * 2.1f);
            const float d = std::abs(u - c) * 7.f;
            v = juce::jmax(v, juce::jlimit(0.f, 1.f, 1.f - d));
        }
        g.setColour(kMagenta.interpolatedWith(kLime, 0.5f + 0.5f * std::sin(t * 0.7f + u * 5.f)).withAlpha(0.55f * v));
        g.fillRect(0, barTop + y, kW, 1);
    }

    // starfield (top area)
    for (const auto& s : stars) {
        g.setColour(juce::Colours::white.withAlpha(0.25f + 0.75f * (s.v / 2.f)));
        const int sz = s.v > 1.3f ? 2 : 1;
        g.fillRect((int) s.x, (int) s.y, sz, sz);
    }

    if (logoImage().isValid())
        g.drawImageWithin(logoImage(), 90, 6, 240, 52, juce::RectanglePlacement::centred);

    g.setFont(mono(13.f));
    g.setColour(kLime);
    g.drawText("-=[ NEUROFORGE v" + versionText() + " KEYGEN ]=-", 0, 62, kW, 16, juce::Justification::centred);
    g.setFont(mono(10.5f, false));
    g.setColour(kMagenta);
    g.drawText("cracked by nobody, because it is FREE.", 0, 80, kW, 13, juce::Justification::centred);
    g.setColour(tok::textLabel);
    g.drawText("Please enjoy for free, by TuigNL.", 0, 93, kW, 13, juce::Justification::centred);

    auto field = [&](int y, const juce::String& label, const juce::String& value, bool cursor) {
        g.setFont(mono(12.f));
        g.setColour(kLime);
        g.drawText(label, 30, y, 64, 22, juce::Justification::centredLeft);
        const juce::Rectangle<int> r(96, y, 294, 22);
        g.setColour(juce::Colour(0xff06140a));
        g.fillRect(r);
        g.setColour(kLime.withAlpha(0.8f));
        g.drawRect(r, 1);
        g.setColour(kCyan);
        g.drawText(value + (cursor ? "_" : ""), r.reduced(6, 0), juce::Justification::centredLeft);
    };
    const bool blink = std::fmod(t, 1.f) < 0.5f;
    field(122, "NAME:", nameText, false);
    const int typed = juce::jlimit(0, serial.length(), (int) ((now - serialT0) / 35.0));
    field(150, "SERIAL:", serial.substring(0, typed), blink || typed < serial.length());

    auto button = [&](juce::Rectangle<int> r, const juce::String& txt, juce::Colour c, bool hot) {
        r = r.translated(-box().getX(), -box().getY());
        g.setColour(hot ? c : juce::Colours::black);
        g.fillRect(r);
        g.setColour(c);
        g.drawRect(r, 2);
        g.setFont(mono(12.f));
        g.setColour(hot ? juce::Colours::black : c);
        g.drawText(txt, r, juce::Justification::centred);
    };
    button(genRect(), "GENERATE", kLime, hover == 1);
    button(exitRect(), "EXIT", kMagenta, hover == 2);

    // sine-wave scroller
    static const juce::String msg =
        "   *** NEUROFORGE ***   greetz to all neuro heads ... respect to the bass scientists ... "
        "released under AGPLv3 ... no serial needed, ever ... "
        "no cd-key, no dongle, no nag screen, just filth ... TuigNL 2026 ...   ";
    const auto f = mono(16.f);
    g.setFont(f);
    const float cw = f.getStringWidthFloat("W");
    const float total = cw * (float) msg.length();
    const float off = std::fmod(t * 90.f, total);
    for (int i = 0; i < msg.length(); ++i) {
        float x = (float) kW - off + cw * (float) i;
        if (x < -cw) x += total;
        if (x < -cw || x > (float) kW) continue;
        const float y = (float) barTop + 14.f + 11.f * std::sin(x * 0.035f + t * 4.f);
        const auto ch = juce::String::charToString(msg[i]);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawText(ch, (int) x + 1, (int) y + 1, (int) cw + 2, 20, juce::Justification::centred);
        g.setColour(kLime.interpolatedWith(juce::Colours::white, 0.5f + 0.5f * std::sin(x * 0.02f - t * 3.f)));
        g.drawText(ch, (int) x, (int) y, (int) cw + 2, 20, juce::Justification::centred);
    }

    g.setFont(mono(10.5f, false));
    g.setColour(tok::textSecondary);
    g.drawText(juce::String("> status: 100% legit, 0% cracked") + (blink ? "_" : " "), 14, 274, kW - 28, 16, juce::Justification::centredLeft);
    g.drawText("[Esc] close", 14, 274, kW - 28, 16, juce::Justification::centredRight);

    g.setColour(kMagenta);
    g.drawRect(0, 0, kW, kH, 2);
    g.setColour(kLime);
    g.drawRect(4, 4, kW - 8, kH - 8, 1);
}

} // namespace nfui
