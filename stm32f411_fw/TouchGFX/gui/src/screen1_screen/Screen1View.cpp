#include <gui/screen1_screen/Screen1View.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>

namespace
{
const int BAR_TRACK_W = 194;
const uint8_t APPS_VALID_FLAG = 1U << 0;

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

void formatPerMille(touchgfx::Unicode::UnicodeChar* buffer,
                    uint16_t bufferSize,
                    uint16_t value)
{
    touchgfx::Unicode::snprintf(buffer,
                                bufferSize,
                                "%u.%u%%",
                                value / 10U,
                                value % 10U);
}
}

Screen1View::Screen1View()
{
    pedalText.setPosition(30, 142, 210, 50);
    pedalText.setColor(touchgfx::Color::getColorFromRGB(255, 255, 255));
    pedalText.setTypedText(touchgfx::TypedText(T_T_RPM));
    pedalText.setWildcard(pedalBuffer);
    add(pedalText);

    apps1Text.setPosition(344, 68, 120, 30);
    apps1Text.setColor(touchgfx::Color::getColorFromRGB(83, 211, 255));
    apps1Text.setTypedText(touchgfx::TypedText(T_T_VOUTVAL));
    apps1Text.setWildcard(apps1Buffer);
    add(apps1Text);

    apps2Text.setPosition(344, 128, 120, 30);
    apps2Text.setColor(touchgfx::Color::getColorFromRGB(255, 197, 61));
    apps2Text.setTypedText(touchgfx::TypedText(T_T_MOTORVAL));
    apps2Text.setWildcard(apps2Buffer);
    add(apps2Text);

    commandText.setPosition(344, 188, 120, 30);
    commandText.setColor(touchgfx::Color::getColorFromRGB(65, 220, 114));
    commandText.setTypedText(touchgfx::TypedText(T_T_IGBTVAL));
    commandText.setWildcard(commandBuffer);
    add(commandText);

    faultText.setPosition(314, 248, 150, 24);
    faultText.setColor(touchgfx::Color::getColorFromRGB(230, 237, 243));
    faultText.setTypedText(touchgfx::TypedText(T_T_TORQUEVAL));
    faultText.setWildcard(faultBuffer);
    add(faultText);
}

void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();

    socTrack.setVisible(false);
    socFill.setVisible(false);
    socText.setVisible(false);

    rpmValue.setVisible(false);
    voutVal.setVisible(false);
    motorVal.setVisible(false);
    igbtVal.setVisible(false);
    torqueVal.setVisible(false);

    rpmRing.setRange(0, 1000);
    rpmRing.setValue(0);

    updateApps(false, 0U, 0U, 0U, 0U, 0, 0U, 0U);
}

void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

void Screen1View::updateApps(bool connected,
                             uint8_t flags,
                             uint16_t apps1PerMille,
                             uint16_t apps2PerMille,
                             uint16_t pedalPerMille,
                             int16_t command,
                             uint32_t status,
                             uint32_t fault)
{
    touchgfx::colortype readyColor =
        touchgfx::Color::getColorFromRGB(65, 220, 114);
    touchgfx::colortype faultColor =
        touchgfx::Color::getColorFromRGB(255, 76, 76);
    touchgfx::colortype lostColor =
        touchgfx::Color::getColorFromRGB(255, 197, 61);
    uint32_t displayedFault = status | fault;
    bool faultActive = ((flags & APPS_VALID_FLAG) == 0U) ||
                       (status != 0U) ||
                       (fault != 0U);

    if (pedalPerMille > 1000U)
    {
        pedalPerMille = 1000U;
    }
    if (apps1PerMille > 1000U)
    {
        apps1PerMille = 1000U;
    }
    if (apps2PerMille > 1000U)
    {
        apps2PerMille = 1000U;
    }

    rpmRing.setValue(pedalPerMille);
    setBar(voutFill, apps1PerMille, 1000, BAR_TRACK_W);
    setBar(motorFill, apps2PerMille, 1000, BAR_TRACK_W);
    setBar(igbtFill, command < 0 ? 0 : command, 32767, BAR_TRACK_W);
    setBar(phaseFill, faultActive ? 1 : 0, 1, BAR_TRACK_W);

    formatPerMille(pedalBuffer, 8U, pedalPerMille);
    formatPerMille(apps1Buffer, 8U, apps1PerMille);
    formatPerMille(apps2Buffer, 8U, apps2PerMille);
    touchgfx::Unicode::snprintf(commandBuffer, 8U, "%d", command);
    touchgfx::Unicode::snprintf(faultBuffer,
                                11U,
                                "0x%08x",
                                static_cast<unsigned int>(displayedFault));

    pedalText.invalidate();
    apps1Text.invalidate();
    apps2Text.invalidate();
    commandText.invalidate();
    faultText.invalidate();

    if (!connected)
    {
        statusText.setTypedText(touchgfx::TypedText(T_T_DATALOST));
        statusText.setColor(lostColor);
        statusDot.setColor(lostColor);
    }
    else if (faultActive)
    {
        statusText.setTypedText(touchgfx::TypedText(T_T_STATUSFAULT));
        statusText.setColor(faultColor);
        statusDot.setColor(faultColor);
    }
    else
    {
        statusText.setTypedText(touchgfx::TypedText(T_T_STATUS));
        statusText.setColor(readyColor);
        statusDot.setColor(readyColor);
    }

    statusText.invalidate();
    statusDot.invalidate();
}
