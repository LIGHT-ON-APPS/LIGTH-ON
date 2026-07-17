#include <gui/screen1_screen/Screen1View.hpp>
#include <touchgfx/Color.hpp>

namespace
{
const int BAR_TRACK_W = 288;
const int SOC_TRACK_W = 120;

void setBar(touchgfx::Box& fill, int val, int maxVal, int trackW)
{
    if (val < 0)
    {
        val = 0;
    }
    if (val > maxVal)
    {
        val = maxVal;
    }

    int w = (int)((long)val * trackW / maxVal);
    fill.invalidate();
    fill.setWidth((uint16_t)w);
    fill.invalidate();
}

void setZone(touchgfx::Box& fill, int val, int warn, int danger)
{
    touchgfx::colortype c = touchgfx::Color::getColorFromRGB(65, 220, 114);
    if (val >= danger)
    {
        c = touchgfx::Color::getColorFromRGB(255, 76, 76);
    }
    else if (val >= warn)
    {
        c = touchgfx::Color::getColorFromRGB(255, 197, 61);
    }
    fill.setColor(c);
}
}

Screen1View::Screen1View()
{
}

void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();
}

void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

void Screen1View::updateDash(int rpm, int motorTemp, int igbtTemp, int phaseCurrent, int soc, int vout)
{
    rpmRing.setValue(rpm);

    if (vout < 220)
    {
        voutFill.setColor(touchgfx::Color::getColorFromRGB(255, 76, 76));
    }
    else if (vout < 300)
    {
        voutFill.setColor(touchgfx::Color::getColorFromRGB(255, 197, 61));
    }
    else
    {
        voutFill.setColor(touchgfx::Color::getColorFromRGB(83, 211, 255));
    }
    setBar(voutFill, vout, 500, BAR_TRACK_W);

    setZone(motorFill, motorTemp, 90, 110);
    setBar(motorFill, motorTemp, 120, BAR_TRACK_W);

    setZone(igbtFill, igbtTemp, 73, 82);
    setBar(igbtFill, igbtTemp, 120, BAR_TRACK_W);

    setBar(phaseFill, phaseCurrent, 200, BAR_TRACK_W);
    setBar(socFill, soc, 100, SOC_TRACK_W);
}