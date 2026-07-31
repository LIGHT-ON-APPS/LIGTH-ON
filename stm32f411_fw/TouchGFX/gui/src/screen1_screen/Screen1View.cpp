#include <gui/screen1_screen/Screen1View.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>

namespace
{
/*
 * 야간 주행용 색. 순백/순흑을 쓰지 않는다. 어두운 곳에서 순백 글자는 번져
 * 보이고(halation) 잔상이 남아 오히려 읽기 어렵다. 배경은 완전한 검정 대신
 * 약간 들어올려 패널 경계가 보이게 한다.
 */
const uint8_t BG_R = 8,    BG_G = 11,   BG_B = 16;
const uint8_t TILE_R = 20, TILE_G = 26, TILE_B = 34;
const uint8_t RULE_R = 42, RULE_G = 52, RULE_B = 64;
const uint8_t TRACK_R = 32, TRACK_G = 40, TRACK_B = 50;

const uint8_t FG_R = 228, FG_G = 235, FG_B = 242;   /* 주 텍스트 */
const uint8_t DIM_R = 132, DIM_G = 146, DIM_B = 160; /* 라벨/무효값 */

const uint8_t OK_R = 60,   OK_G = 205, OK_B = 120;
const uint8_t WARN_R = 255, WARN_G = 188, WARN_B = 60;
const uint8_t FAULT_R = 255, FAULT_G = 84, FAULT_B = 84;
const uint8_t ACCENT_R = 88, ACCENT_G = 196, ACCENT_B = 248;

const uint8_t APPS_VALID_FLAG = 1U << 0;

/*
 * 온도 경고 임계값 [0.1 degC]. --- 팀에서 확정해야 하는 값 ---
 * 셀은 리튬이온 일반값, IGBT 는 Bamocar 디레이팅 시작점 근처를 임시로 잡았다.
 * 실제 팩/인버터 사양서로 반드시 대조할 것. 색이 바뀌는 지점이라 운전자 판단에
 * 직접 영향을 준다.
 */
const int16_t CELL_WARN_DECIC = 450;   /* 45.0 C */
const int16_t CELL_CRIT_DECIC = 550;   /* 55.0 C */
const int16_t IGBT_WARN_DECIC = 600;   /* 60.0 C */
const int16_t IGBT_CRIT_DECIC = 750;   /* 75.0 C */

touchgfx::colortype rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return touchgfx::Color::getColorFromRGB(r, g, b);
}

void placeBox(touchgfx::Box& box, int16_t x, int16_t y, int16_t w, int16_t h,
              uint8_t r, uint8_t g, uint8_t b)
{
    box.setPosition(x, y, w, h);
    box.setColor(rgb(r, g, b));
}

void placeLabel(touchgfx::TextArea& text, int16_t x, int16_t y,
                int16_t w, int16_t h, TEXTS id)
{
    text.setPosition(x, y, w, h);
    text.setColor(rgb(DIM_R, DIM_G, DIM_B));
    text.setTypedText(touchgfx::TypedText(id));
}

void placeValue(touchgfx::TextAreaWithOneWildcard& text,
                touchgfx::Unicode::UnicodeChar* buffer,
                int16_t x, int16_t y, int16_t w, int16_t h, TEXTS id)
{
    text.setPosition(x, y, w, h);
    text.setColor(rgb(FG_R, FG_G, FG_B));
    text.setTypedText(touchgfx::TypedText(id));
    text.setWildcard(buffer);
}

/* 막대 채움. 트랙과 같은 위치에서 폭만 바꾼다. */
void setBar(touchgfx::Box& fill, int32_t value, int32_t maxValue,
            int16_t trackWidth)
{
    int32_t width;

    if (value < 0)
    {
        value = 0;
    }
    if (maxValue <= 0)
    {
        maxValue = 1;
    }
    if (value > maxValue)
    {
        value = maxValue;
    }

    width = (value * trackWidth) / maxValue;

    fill.invalidate();
    fill.setWidth(static_cast<uint16_t>(width));
    fill.invalidate();
}

/* 0.1 단위 정수를 "12.3" 으로. 음수도 부호가 한 번만 붙게 처리한다. */
void formatDeci(touchgfx::Unicode::UnicodeChar* buffer, uint16_t size,
                int32_t deci)
{
    int32_t whole = deci / 10;
    int32_t frac = deci % 10;

    if (frac < 0)
    {
        frac = -frac;
    }

    if ((deci < 0) && (whole == 0))
    {
        /* -0.4 처럼 정수부가 0 인 음수는 "0.4" 로 나와버린다. */
        touchgfx::Unicode::snprintf(buffer, size, "-0.%d",
                                    static_cast<int>(frac));
        return;
    }

    touchgfx::Unicode::snprintf(buffer, size, "%d.%d",
                                static_cast<int>(whole),
                                static_cast<int>(frac));
}

/* 값이 없을 때. 숫자 자리를 비우면 운전자가 0 으로 오해한다. */
void formatUnknown(touchgfx::Unicode::UnicodeChar* buffer, uint16_t size)
{
    touchgfx::Unicode::snprintf(buffer, size, "--");
}
}

Screen1View::Screen1View()
    : appsFaultActive(false),
      linkConnected(false)
{
    /* ---------------- 배경 ---------------- */
    placeBox(screenBg, 0, 0, 480, 320, BG_R, BG_G, BG_B);
    add(screenBg);

    /* ---------------- 상태 바 ---------------- */
    placeBox(linkDot, 16, 15, 14, 14, DIM_R, DIM_G, DIM_B);
    add(linkDot);

    linkText.setPosition(40, 9, 200, 26);
    linkText.setColor(rgb(FG_R, FG_G, FG_B));
    linkText.setTypedText(touchgfx::TypedText(T_T_DATALOST));
    add(linkText);

    placeLabel(socLabel, 306, 14, 36, 18, T_T_SOCLABEL);
    add(socLabel);
    placeValue(socValue, socBuf, 344, 9, 96, 26, T_T_SOC);
    add(socValue);
    placeLabel(socPercent, 444, 14, 20, 18, T_T_PERCENT);
    add(socPercent);

    placeBox(headerRule, 0, 44, 480, 2, RULE_R, RULE_G, RULE_B);
    add(headerRule);

    /* ---------------- RPM (주 수치) ---------------- */
    placeValue(rpmText, rpmBuf, 12, 50, 280, 76, T_T_RPM);
    add(rpmText);
    placeLabel(rpmUnitText, 12, 124, 280, 18, T_T_RPMUNIT);
    add(rpmUnitText);

    /* ---------------- 속도 ---------------- */
    placeValue(speedText, speedBuf, 12, 150, 280, 46, T_T_VOUTVAL);
    speedText.setColor(rgb(ACCENT_R, ACCENT_G, ACCENT_B));
    add(speedText);
    placeLabel(speedUnitText, 12, 194, 280, 18, T_T_SPEEDUNIT);
    add(speedUnitText);

    /* ---------------- 온도 타일 ---------------- */
    placeBox(cellTile, 300, 52, 168, 76, TILE_R, TILE_G, TILE_B);
    add(cellTile);
    placeBox(cellAccent, 300, 52, 4, 76, OK_R, OK_G, OK_B);
    add(cellAccent);
    placeLabel(cellTempLabel, 314, 58, 146, 18, T_T_MOTORLABEL);
    add(cellTempLabel);
    placeValue(cellValue, cellBuf, 306, 78, 150, 40, T_T_MOTORVAL);
    add(cellValue);

    placeBox(igbtTile, 300, 136, 168, 76, TILE_R, TILE_G, TILE_B);
    add(igbtTile);
    placeBox(igbtAccent, 300, 136, 4, 76, OK_R, OK_G, OK_B);
    add(igbtAccent);
    placeLabel(igbtTempLabel, 314, 142, 146, 18, T_T_IGBTLABEL);
    add(igbtTempLabel);
    placeValue(igbtValue, igbtBuf, 306, 162, 150, 40, T_T_IGBTVAL);
    add(igbtValue);

    /* ---------------- 아래 구획 ---------------- */
    placeBox(footerRule, 0, 222, 480, 2, RULE_R, RULE_G, RULE_B);
    add(footerRule);

    placeLabel(pedalLabel, 12, 232, 64, 18, T_T_TORQUELABEL);
    add(pedalLabel);
    placeBox(pedalTrack, 80, 236, 264, 12, TRACK_R, TRACK_G, TRACK_B);
    add(pedalTrack);
    placeBox(pedalFillBar, 80, 236, 0, 12, ACCENT_R, ACCENT_G, ACCENT_B);
    add(pedalFillBar);
    placeValue(pedalValue, pedalBuf, 348, 228, 96, 24, T_T_TORQUEVAL);
    add(pedalValue);
    placeLabel(pedalPercent, 448, 232, 20, 18, T_T_PERCENT);
    add(pedalPercent);

    placeLabel(ain1Label, 12, 262, 50, 18, T_T_AIN1LABEL);
    add(ain1Label);
    placeBox(ain1Track, 66, 266, 92, 10, TRACK_R, TRACK_G, TRACK_B);
    add(ain1Track);
    placeBox(ain1FillBar, 66, 266, 0, 10, DIM_R, DIM_G, DIM_B);
    add(ain1FillBar);
    placeValue(ain1Value, ain1Buf, 160, 258, 64, 22, T_T_AIN1VAL);
    add(ain1Value);

    placeLabel(ain2Label, 242, 262, 50, 18, T_T_AIN2LABEL);
    add(ain2Label);
    placeBox(ain2Track, 296, 266, 92, 10, TRACK_R, TRACK_G, TRACK_B);
    add(ain2Track);
    placeBox(ain2FillBar, 296, 266, 0, 10, DIM_R, DIM_G, DIM_B);
    add(ain2FillBar);
    placeValue(ain2Value, ain2Buf, 390, 258, 74, 22, T_T_AIN2VAL);
    add(ain2Value);

    placeLabel(packLabel, 12, 292, 46, 18, T_T_PACKVLABEL);
    add(packLabel);
    placeValue(packValue, packBuf, 56, 288, 90, 24, T_T_PACKVVAL);
    add(packValue);
    placeLabel(packUnit, 150, 292, 16, 18, T_T_VOLT);
    add(packUnit);

    placeLabel(ampLabel, 242, 292, 46, 18, T_T_PACKALABEL);
    add(ampLabel);
    placeValue(ampValue, ampBuf, 286, 288, 90, 24, T_T_PACKAVAL);
    add(ampValue);
    placeLabel(ampUnit, 380, 292, 16, 18, T_T_AMP);
    add(ampUnit);
}

void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();

    /*
     * Designer 가 배치한 위젯은 전부 감춘다. 이 화면은 위 생성자에서 직접
     * 구성했고, 두 벌이 겹쳐 그려지면 잔상이 남는다.
     */
    __background.setVisible(false);
    background.setVisible(false);
    topDivider.setVisible(false);
    socTrack.setVisible(false);
    socFill.setVisible(false);
    voutTrack.setVisible(false);
    voutFill.setVisible(false);
    motorTrack.setVisible(false);
    motorFill.setVisible(false);
    igbtTrack.setVisible(false);
    igbtFill.setVisible(false);
    phaseTrack.setVisible(false);
    phaseFill.setVisible(false);
    rpmRing.setVisible(false);
    statusDot.setVisible(false);
    statusText.setVisible(false);
    socText.setVisible(false);
    rpmValue.setVisible(false);
    rpmUnit.setVisible(false);
    voutLabel.setVisible(false);
    voutVal.setVisible(false);
    motorLabel.setVisible(false);
    motorVal.setVisible(false);
    igbtLabel.setVisible(false);
    igbtVal.setVisible(false);
    torqueLabel.setVisible(false);
    torqueVal.setVisible(false);

    updateApps(false, 0U, 0U, 0U, 0U, 0U, 0U, 0, 0U, 0U);
    updateTelemetry(false, 0U, 0, 0U, 0U, 0, 0U, 0, 0);
}

void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

void Screen1View::updateApps(bool connected,
                             uint8_t flags,
                             uint16_t apps1Raw,
                             uint16_t apps2Raw,
                             uint16_t apps1PerMille,
                             uint16_t apps2PerMille,
                             uint16_t pedalPerMille,
                             int16_t command,
                             uint32_t status,
                             uint32_t fault)
{
    (void)command;

    linkConnected = connected;
    appsFaultActive = ((flags & APPS_VALID_FLAG) == 0U) ||
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

    if (connected)
    {
        setBar(pedalFillBar, pedalPerMille, 1000, 264);
        setBar(ain1FillBar, apps1PerMille, 1000, 92);
        setBar(ain2FillBar, apps2PerMille, 1000, 92);

        formatDeci(pedalBuf, 10U, pedalPerMille);
        /* 아날로그 입력은 원시 ADC 카운트를 그대로 보여준다. 보정 전 값이라야
         * 센서 배선 문제를 계기에서 바로 알아볼 수 있다. */
        touchgfx::Unicode::snprintf(ain1Buf, 10U, "%u",
                                    static_cast<unsigned int>(apps1Raw));
        touchgfx::Unicode::snprintf(ain2Buf, 10U, "%u",
                                    static_cast<unsigned int>(apps2Raw));
    }
    else
    {
        setBar(pedalFillBar, 0, 1000, 264);
        setBar(ain1FillBar, 0, 1000, 92);
        setBar(ain2FillBar, 0, 1000, 92);
        formatUnknown(pedalBuf, 10U);
        formatUnknown(ain1Buf, 10U);
        formatUnknown(ain2Buf, 10U);
    }

    /* 페달 막대는 fault 일 때 붉게. 색만으로도 즉시 구분되게 한다. */
    pedalFillBar.setColor(appsFaultActive ? rgb(FAULT_R, FAULT_G, FAULT_B)
                                          : rgb(ACCENT_R, ACCENT_G, ACCENT_B));

    if (!connected)
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_DATALOST));
        linkText.setColor(rgb(WARN_R, WARN_G, WARN_B));
        linkDot.setColor(rgb(WARN_R, WARN_G, WARN_B));
    }
    else if (appsFaultActive)
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_STATUSFAULT));
        linkText.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        linkDot.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
    }
    else
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_STATUS));
        linkText.setColor(rgb(OK_R, OK_G, OK_B));
        linkDot.setColor(rgb(OK_R, OK_G, OK_B));
    }

    pedalValue.invalidate();
    ain1Value.invalidate();
    ain2Value.invalidate();
    pedalFillBar.invalidate();
    linkText.invalidate();
    linkDot.invalidate();
}

void Screen1View::updateTelemetry(bool fresh,
                                  uint32_t validMask,
                                  int32_t motorRpm,
                                  uint16_t speedDkph,
                                  uint16_t packDv,
                                  int16_t packDa,
                                  uint16_t socPerMille,
                                  int16_t cellTempDecic,
                                  int16_t igbtTempDecic)
{
    /*
     * 각 값은 '텔레메트리가 최근에 왔고' + '그 필드의 valid 비트가 서 있을 때'만
     * 숫자로 띄운다. 둘 중 하나라도 아니면 '--'. CAN 이 끊긴 뒤 마지막 숫자가
     * 화면에 그대로 남아 있으면 운전자가 현재 값으로 읽는다.
     */
    const bool haveRpm = fresh && ((validMask & (1UL << 5)) != 0U);
    const bool haveSpeed = fresh && ((validMask & (1UL << 16)) != 0U);
    const bool havePackV = fresh && ((validMask & (1UL << 0)) != 0U);
    const bool havePackA = fresh && ((validMask & (1UL << 1)) != 0U);
    const bool haveSoc = fresh && ((validMask & (1UL << 2)) != 0U);
    const bool haveCell = fresh && ((validMask & (1UL << 3)) != 0U);
    const bool haveIgbt = fresh && ((validMask & (1UL << 9)) != 0U);

    if (haveRpm)
    {
        touchgfx::Unicode::snprintf(rpmBuf, 10U, "%d",
                                    static_cast<int>(motorRpm));
        rpmText.setColor(rgb(FG_R, FG_G, FG_B));
    }
    else
    {
        formatUnknown(rpmBuf, 10U);
        rpmText.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    if (haveSpeed)
    {
        formatDeci(speedBuf, 10U, speedDkph);
        speedText.setColor(rgb(ACCENT_R, ACCENT_G, ACCENT_B));
    }
    else
    {
        /* 감속비/타이어 제원이 설정되지 않으면 L432 가 이 비트를 세우지 않는다.
         * 추정 속도를 띄우느니 비워 둔다. */
        formatUnknown(speedBuf, 10U);
        speedText.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    if (haveCell)
    {
        formatDeci(cellBuf, 10U, cellTempDecic);
        if (cellTempDecic >= CELL_CRIT_DECIC)
        {
            cellValue.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
            cellAccent.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        }
        else if (cellTempDecic >= CELL_WARN_DECIC)
        {
            cellValue.setColor(rgb(WARN_R, WARN_G, WARN_B));
            cellAccent.setColor(rgb(WARN_R, WARN_G, WARN_B));
        }
        else
        {
            cellValue.setColor(rgb(FG_R, FG_G, FG_B));
            cellAccent.setColor(rgb(OK_R, OK_G, OK_B));
        }
    }
    else
    {
        formatUnknown(cellBuf, 10U);
        cellValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
        cellAccent.setColor(rgb(RULE_R, RULE_G, RULE_B));
    }

    if (haveIgbt)
    {
        formatDeci(igbtBuf, 10U, igbtTempDecic);
        if (igbtTempDecic >= IGBT_CRIT_DECIC)
        {
            igbtValue.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
            igbtAccent.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        }
        else if (igbtTempDecic >= IGBT_WARN_DECIC)
        {
            igbtValue.setColor(rgb(WARN_R, WARN_G, WARN_B));
            igbtAccent.setColor(rgb(WARN_R, WARN_G, WARN_B));
        }
        else
        {
            igbtValue.setColor(rgb(FG_R, FG_G, FG_B));
            igbtAccent.setColor(rgb(OK_R, OK_G, OK_B));
        }
    }
    else
    {
        formatUnknown(igbtBuf, 10U);
        igbtValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
        igbtAccent.setColor(rgb(RULE_R, RULE_G, RULE_B));
    }

    if (haveSoc)
    {
        formatDeci(socBuf, 10U, socPerMille);
        socValue.setColor(rgb(FG_R, FG_G, FG_B));
    }
    else
    {
        formatUnknown(socBuf, 10U);
        socValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    if (havePackV)
    {
        formatDeci(packBuf, 10U, packDv);
        packValue.setColor(rgb(FG_R, FG_G, FG_B));
    }
    else
    {
        formatUnknown(packBuf, 10U);
        packValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    if (havePackA)
    {
        formatDeci(ampBuf, 10U, packDa);
        ampValue.setColor(rgb(FG_R, FG_G, FG_B));
    }
    else
    {
        formatUnknown(ampBuf, 10U);
        ampValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    rpmText.invalidate();
    speedText.invalidate();
    cellValue.invalidate();
    cellAccent.invalidate();
    igbtValue.invalidate();
    igbtAccent.invalidate();
    socValue.invalidate();
    packValue.invalidate();
    ampValue.invalidate();
}
