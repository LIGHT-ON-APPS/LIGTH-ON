#include <gui/screen1_screen/Screen1View.hpp>
#include <touchgfx/Color.hpp>
#include <texts/TextKeysAndLanguages.hpp>

namespace
{
/*
 * 야간 주행용 색. 순백/순흑을 쓰지 않는다. 어두운 곳에서 순백 글자는 번져
 * 보이고(halation) 잔상이 남아 오히려 읽기 어렵다. 배경은 완전한 검정 대신
 * 약간 들어올려 게이지 트랙이 배경과 구분되게 한다.
 */
const uint8_t BG_R = 8,     BG_G = 11,   BG_B = 16;
const uint8_t RULE_R = 32,  RULE_G = 40, RULE_B = 50;
const uint8_t TRACK_R = 30, TRACK_G = 37, TRACK_B = 46;
const uint8_t SEGOFF_R = 24, SEGOFF_G = 30, SEGOFF_B = 38;

const uint8_t FG_R = 238,  FG_G = 244, FG_B = 250;   /* 주 수치 */
const uint8_t DIM_R = 124, DIM_G = 140, DIM_B = 155; /* 라벨/무효값 */

const uint8_t RPM_R = 120,  RPM_G = 216, RPM_B = 255; /* RPM 아크 */
const uint8_t OK_R = 62,    OK_G = 208,  OK_B = 122;
const uint8_t WARN_R = 255, WARN_G = 190, WARN_B = 60;
const uint8_t FAULT_R = 255, FAULT_G = 76, FAULT_B = 76;

const uint8_t APPS_VALID_FLAG = 1U << 0;

/*
 * --- 팀에서 확정해야 하는 값 ---
 *
 * 모터 최대 회전수. 게이지 눈금 전체와 시프트 라이트 구간이 전부 이 값에서
 * 나오므로, 실제 모터/인버터 사양과 다르면 계기판 전체가 틀린 눈금이 된다.
 *
 * 4자리(<= 9999)를 전제로 가운데 숫자 폰트 크기를 잡았다. 이보다 큰 모터를
 * 쓰게 되면 숫자가 게이지 링을 침범하므로 폰트를 Large 로 낮춰야 한다.
 */
const int32_t RPM_MAX = 6500;

/* 시프트 라이트가 노랑/빨강으로 넘어가는 지점(RPM_MAX 대비 per-mille).
 * 빨강 구간은 회전수 한계에 다 왔다는 뜻이고, 점멸은 레드라인이다. */
const int32_t SHIFT_AMBER_PM = 700;
const int32_t SHIFT_RED_PM = 860;
const int32_t SHIFT_BLINK_PM = 960;

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

/* SOC 경고 임계값 [per-mille]. */
const int32_t SOC_WARN_PM = 300;
const int32_t SOC_CRIT_PM = 150;

/* --- 레이아웃 --- */
const int16_t SEG_Y = 4;
const int16_t SEG_H = 30;
const int16_t SEG_W = 27;
const int16_t SEG_PITCH = 30;
const int16_t SEG_X0 = 2;

const int16_t GAUGE_BOX = 198;         /* 게이지 위젯 한 변 */
const int16_t GAUGE_Y = 46;
const int16_t GAUGE_R = 90;
/* 링을 더 굵게 하면 안쪽 지름이 좁아져 4자리 RPM 이 링을 파고든다.
 * 14 는 링이 계기판으로 보이는 최소 굵기와 숫자 여백이 만나는 지점이다. */
const int16_t GAUGE_LW = 14;
const int16_t GAUGE_L_X = 6;           /* 좌(RPM) 위젯 좌상단 */
const int16_t GAUGE_R_X = 276;         /* 우(SOC) 위젯 좌상단 */
const int16_t GAUGE_C = GAUGE_BOX / 2; /* 위젯 안에서의 중심 */

/* 계기판 관례대로 7시30분에서 4시30분까지 270도. */
const int16_t ARC_START = -135;
const int16_t ARC_END = 135;

const int16_t VALUE_Y = 108;
const int16_t VALUE_H = 78;
const int16_t LABEL_Y = 190;

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

/* 게이지 바탕이 되는 빈 링. 값이 0일 때도 눈금이 어디까지 있는지 보여야
 * 게이지로 읽힌다. */
void placeTrack(touchgfx::Circle& circle, touchgfx::PainterRGB565& painter,
                int16_t x, int16_t y)
{
    circle.setPosition(x, y, GAUGE_BOX, GAUGE_BOX);
    circle.setCenter(GAUGE_C, GAUGE_C);
    circle.setRadius(GAUGE_R);
    circle.setLineWidth(GAUGE_LW);
    circle.setArc(ARC_START, ARC_END);
    painter.setColor(rgb(TRACK_R, TRACK_G, TRACK_B));
    circle.setPainter(painter);
}

void placeArc(touchgfx::CircleProgress& arc, touchgfx::PainterRGB565& painter,
              int16_t x, int16_t y, int32_t range)
{
    arc.setXY(x, y);
    arc.setProgressIndicatorPosition(0, 0, GAUGE_BOX, GAUGE_BOX);
    arc.setCenter(GAUGE_C, GAUGE_C);
    arc.setRadius(GAUGE_R);
    arc.setLineWidth(GAUGE_LW);
    arc.setStartEndAngle(ARC_START, ARC_END);
    arc.setCapPrecision(180);
    arc.setRange(0, static_cast<int>(range));
    arc.setPainter(painter);
    arc.setValue(0);
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
      linkConnected(false),
      shiftBlinkActive(false),
      shiftBlinkOn(true),
      blinkTick(0U),
      lastRpm(0),
      lastRpmValid(false)
{
    /* ---------------- 배경 ---------------- */
    placeBox(screenBg, 0, 0, 480, 320, BG_R, BG_G, BG_B);
    add(screenBg);

    /* ---------------- 시프트 라이트 ----------------
     * 코너를 보면서도 주변시로 읽는 곳이다. 세그먼트로 끊어야 몇 칸 남았는지
     * 순간적으로 세어지고, 연속 막대보다 변속 시점이 정확해진다. */
    for (uint8_t i = 0U; i < SHIFT_SEGMENTS; i++)
    {
        shiftColor[i] = rgb(SEGOFF_R, SEGOFF_G, SEGOFF_B);
        placeBox(shiftSeg[i],
                 static_cast<int16_t>(SEG_X0 + (i * SEG_PITCH)), SEG_Y,
                 SEG_W, SEG_H,
                 SEGOFF_R, SEGOFF_G, SEGOFF_B);
        add(shiftSeg[i]);
    }
    placeBox(shiftRule, 0, 38, 480, 2, RULE_R, RULE_G, RULE_B);
    add(shiftRule);

    /* ---------------- RPM 게이지 ---------------- */
    placeTrack(rpmTrack, rpmTrackPainter, GAUGE_L_X, GAUGE_Y);
    add(rpmTrack);
    placeArc(rpmArc, rpmArcPainter, GAUGE_L_X, GAUGE_Y, RPM_MAX);
    rpmArcColor = rgb(RPM_R, RPM_G, RPM_B);
    rpmArcPainter.setColor(rpmArcColor);
    add(rpmArc);

    placeValue(rpmText, rpmBuf, GAUGE_L_X, VALUE_Y, GAUGE_BOX, VALUE_H,
               T_T_RPM);
    add(rpmText);
    placeLabel(rpmLabel, GAUGE_L_X, LABEL_Y, GAUGE_BOX, 18, T_T_RPMUNIT);
    add(rpmLabel);

    /* ---------------- SOC 게이지 ---------------- */
    placeTrack(socTrack2, socTrackPainter, GAUGE_R_X, GAUGE_Y);
    add(socTrack2);
    placeArc(socArc, socArcPainter, GAUGE_R_X, GAUGE_Y, 1000);
    socArcPainter.setColor(rgb(OK_R, OK_G, OK_B));
    add(socArc);

    placeValue(socValue, socBuf, GAUGE_R_X, VALUE_Y, GAUGE_BOX, VALUE_H,
               T_T_RPM);
    add(socValue);
    /* "SOC" 와 "%" 를 게이지 중심(375) 기준으로 나란히 놓는다. 라벨 폰트가
     * 왼쪽 정렬이라 x 를 직접 계산해야 가운데로 온다. */
    placeLabel(socLabel, 349, LABEL_Y, 40, 18, T_T_SOCLABEL);
    add(socLabel);
    placeLabel(socPercent, 386, LABEL_Y, 20, 18, T_T_PERCENT);
    add(socPercent);

    /* 두 게이지를 갈라 주는 선. 값이 서로 섞여 읽히지 않게 한다. */
    placeBox(centerRule, 239, 70, 2, 150, RULE_R, RULE_G, RULE_B);
    add(centerRule);

    /* ---------------- 온도 ----------------
     * RPM 게이지 아래에 인버터(IGBT), SOC 게이지 아래에 배터리(CELL) 를 둔다.
     * 위쪽 게이지와 같은 계통끼리 세로로 묶여서 눈이 헤매지 않는다. */
    placeBox(tempRule, 0, 246, 480, 2, RULE_R, RULE_G, RULE_B);
    add(tempRule);

    placeLabel(igbtTempLabel, 12, 264, 90, 18, T_T_IGBTLABEL);
    add(igbtTempLabel);
    placeValue(igbtValue, igbtBuf, 86, 250, 80, 42, T_T_IGBTVAL);
    add(igbtValue);
    placeLabel(igbtUnit, 170, 264, 16, 18, T_T_DEGC);
    add(igbtUnit);

    /* 오른쪽 묶음은 화면 오른쪽 끝에서 왼쪽 묶음과 같은 12px 여백이 남도록
     * 맞춘다. 좌우 여백이 어긋나면 화면이 한쪽으로 밀린 것처럼 보인다. */
    placeLabel(cellTempLabel, 286, 264, 90, 18, T_T_MOTORLABEL);
    add(cellTempLabel);
    placeValue(cellValue, cellBuf, 374, 250, 80, 42, T_T_MOTORVAL);
    add(cellValue);
    placeLabel(cellUnit, 458, 264, 16, 18, T_T_DEGC);
    add(cellUnit);

    /* ---------------- 링크 상태 ----------------
     * 정상일 때는 눈에 띌 필요가 없는 정보라 화면 맨 아래 작게 둔다.
     * 문제가 생기면 색으로 올라온다. */
    placeBox(linkDot, 196, 300, 12, 12, DIM_R, DIM_G, DIM_B);
    add(linkDot);

    linkText.setPosition(216, 294, 120, 24);
    linkText.setColor(rgb(DIM_R, DIM_G, DIM_B));
    linkText.setTypedText(touchgfx::TypedText(T_T_DATALOST));
    add(linkText);
}

void Screen1View::setupScreen()
{
    Screen1ViewBase::setupScreen();

    /*
     * Designer 가 배치한 위젯은 전부 감춘다. 이 화면은 위 생성자에서 직접
     * 구성했고, 두 벌이 겹쳐 그려지면 잔상이 남는다.
     *
     * 전부 Screen1ViewBase:: 로 한정한다. 여기 이름 중 하나가 이 클래스의 멤버와
     * 겹치면 파생 멤버가 base 를 가려서, 감추려던 Designer 위젯 대신 이쪽 위젯이
     * 사라진다(값이 통째로 안 보인다). 한정자를 붙여 두면 그때는 컴파일이 되고
     * 동작이 틀리는 대신, 애초에 이 줄이 무엇을 가리키는지가 분명해진다.
     */
    Screen1ViewBase::__background.setVisible(false);
    Screen1ViewBase::background.setVisible(false);
    Screen1ViewBase::topDivider.setVisible(false);
    Screen1ViewBase::socTrack.setVisible(false);
    Screen1ViewBase::socFill.setVisible(false);
    Screen1ViewBase::voutTrack.setVisible(false);
    Screen1ViewBase::voutFill.setVisible(false);
    Screen1ViewBase::motorTrack.setVisible(false);
    Screen1ViewBase::motorFill.setVisible(false);
    Screen1ViewBase::igbtTrack.setVisible(false);
    Screen1ViewBase::igbtFill.setVisible(false);
    Screen1ViewBase::phaseTrack.setVisible(false);
    Screen1ViewBase::phaseFill.setVisible(false);
    Screen1ViewBase::rpmRing.setVisible(false);
    Screen1ViewBase::statusDot.setVisible(false);
    Screen1ViewBase::statusText.setVisible(false);
    Screen1ViewBase::socText.setVisible(false);
    Screen1ViewBase::rpmValue.setVisible(false);
    Screen1ViewBase::rpmUnit.setVisible(false);
    Screen1ViewBase::voutLabel.setVisible(false);
    Screen1ViewBase::voutVal.setVisible(false);
    Screen1ViewBase::motorLabel.setVisible(false);
    Screen1ViewBase::motorVal.setVisible(false);
    Screen1ViewBase::igbtLabel.setVisible(false);
    Screen1ViewBase::igbtVal.setVisible(false);
    Screen1ViewBase::torqueLabel.setVisible(false);
    Screen1ViewBase::torqueVal.setVisible(false);

    updateApps(false, 0U, 0U, 0U, 0U, 0U, 0U, 0, 0U, 0U);
    updateTelemetry(false, 0U, 0, 0U, 0U, 0, 0U, 0, 0);
}

void Screen1View::tearDownScreen()
{
    Screen1ViewBase::tearDownScreen();
}

/*
 * 시프트 라이트를 RPM 에 맞춰 칠한다.
 *
 * 세그먼트는 0 부터 차례로 채우고, 채워진 칸의 색을 구간에 따라 녹/황/적으로
 * 바꾼다. 색이 바뀐 칸만 invalidate 한다 - 부분 프레임버퍼로 SPI 에 실어 보내는
 * 화면이라, 안 바뀐 칸까지 다시 보내면 그대로 프레임 시간이 늘어난다.
 */
void Screen1View::applyShiftLights(int32_t rpm, bool valid)
{
    int32_t per_mille;
    uint8_t lit;

    if (rpm < 0)
    {
        rpm = 0;
    }
    per_mille = (RPM_MAX > 0) ? ((rpm * 1000) / RPM_MAX) : 0;
    if (per_mille > 1000)
    {
        per_mille = 1000;
    }

    /* 값을 못 받는 동안 마지막 RPM 이 그대로 켜져 있으면 운전자가 현재
     * 회전수로 읽는다. 전부 꺼서 '정보 없음' 을 분명히 한다. */
    if (valid != true)
    {
        per_mille = 0;
    }

    lit = static_cast<uint8_t>((per_mille * SHIFT_SEGMENTS) / 1000);
    if (lit > SHIFT_SEGMENTS)
    {
        lit = SHIFT_SEGMENTS;
    }

    shiftBlinkActive = valid && (per_mille >= SHIFT_BLINK_PM);
    if (shiftBlinkActive != true)
    {
        shiftBlinkOn = true;
        blinkTick = 0U;
    }

    for (uint8_t i = 0U; i < SHIFT_SEGMENTS; i++)
    {
        int32_t seg_pm = ((i + 1) * 1000) / SHIFT_SEGMENTS;
        touchgfx::colortype color;

        if (i >= lit)
        {
            color = rgb(SEGOFF_R, SEGOFF_G, SEGOFF_B);
        }
        else if (shiftBlinkActive && (shiftBlinkOn != true))
        {
            /* 레드라인에서는 전체가 함께 꺼졌다 켜진다. 색이 아니라 움직임이라
             * 시선이 계기판에 없어도 알아챈다. */
            color = rgb(SEGOFF_R, SEGOFF_G, SEGOFF_B);
        }
        else if (seg_pm >= SHIFT_RED_PM)
        {
            color = rgb(FAULT_R, FAULT_G, FAULT_B);
        }
        else if (seg_pm >= SHIFT_AMBER_PM)
        {
            color = rgb(WARN_R, WARN_G, WARN_B);
        }
        else
        {
            color = rgb(OK_R, OK_G, OK_B);
        }

        if (color != shiftColor[i])
        {
            shiftColor[i] = color;
            shiftSeg[i].setColor(color);
            shiftSeg[i].invalidate();
        }
    }
}

void Screen1View::handleTickEvent()
{
    if (shiftBlinkActive != true)
    {
        return;
    }

    /* 60 Hz 틱 기준 약 6 Hz 점멸. 더 빠르면 눈이 따라오지 못하고, 더 느리면
     * 변속 신호로 안 읽힌다. */
    blinkTick++;
    if (blinkTick >= 5U)
    {
        blinkTick = 0U;
        shiftBlinkOn = (shiftBlinkOn != true);
        applyShiftLights(lastRpm, lastRpmValid);
    }
}

void Screen1View::applyLinkState()
{
    if (linkConnected != true)
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_DATALOST));
        linkText.setColor(rgb(WARN_R, WARN_G, WARN_B));
        linkDot.setColor(rgb(WARN_R, WARN_G, WARN_B));
        /* 문구마다 폭이 달라서, 가운데로 보이게 하려면 x 를 옮겨야 한다.
         * (Label 은 왼쪽 정렬 타이포그래피다) */
        linkDot.setX(190);
        linkText.setX(210);
    }
    else if (appsFaultActive)
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_STATUSFAULT));
        linkText.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        linkDot.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        linkDot.setX(199);
        linkText.setX(219);
    }
    else
    {
        linkText.setTypedText(touchgfx::TypedText(T_T_STATUS));
        linkText.setColor(rgb(OK_R, OK_G, OK_B));
        linkDot.setColor(rgb(OK_R, OK_G, OK_B));
        linkDot.setX(197);
        linkText.setX(217);
    }

    linkText.invalidate();
    linkDot.invalidate();
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
    /* 페달/아날로그 입력은 이 화면에 띄우지 않는다. 주행 중 드라이버가 볼 값이
     * 아니고, 정비용 수치는 CLI(display info) 로 볼 수 있다. 링크와 고장 상태만
     * 가져다 쓴다. */
    (void)apps1Raw;
    (void)apps2Raw;
    (void)apps1PerMille;
    (void)apps2PerMille;
    (void)pedalPerMille;
    (void)command;

    linkConnected = connected;
    appsFaultActive = ((flags & APPS_VALID_FLAG) == 0U) ||
                      (status != 0U) ||
                      (fault != 0U);

    applyLinkState();
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
    const bool haveSoc = fresh && ((validMask & (1UL << 2)) != 0U);
    const bool haveCell = fresh && ((validMask & (1UL << 3)) != 0U);
    const bool haveIgbt = fresh && ((validMask & (1UL << 9)) != 0U);

    (void)speedDkph;
    (void)packDv;
    (void)packDa;

    /* ---------------- RPM ---------------- */
    lastRpm = motorRpm;
    lastRpmValid = haveRpm;

    if (haveRpm)
    {
        int32_t shown = (motorRpm < 0) ? 0 : motorRpm;
        int32_t per_mille = (RPM_MAX > 0) ? ((shown * 1000) / RPM_MAX) : 0;
        touchgfx::colortype arcColor;

        touchgfx::Unicode::snprintf(rpmBuf, 10U, "%d",
                                    static_cast<int>(shown));
        rpmArc.setValue(static_cast<int>(shown));

        /*
         * 링은 레드라인에서만 색이 바뀐다. 시프트 라이트처럼 노랑 단계까지 두면
         * 주행 시간의 절반을 경고색으로 보내게 되고, 그러면 색이 신호가 아니라
         * 배경이 된다. 중간 구간은 위쪽 세그먼트 바가 이미 보여주고 있다.
         */
        arcColor = (per_mille >= SHIFT_RED_PM) ? rgb(FAULT_R, FAULT_G, FAULT_B)
                                               : rgb(RPM_R, RPM_G, RPM_B);

        rpmText.setColor((per_mille >= SHIFT_RED_PM)
                             ? rgb(FAULT_R, FAULT_G, FAULT_B)
                             : rgb(FG_R, FG_G, FG_B));

        /* 색이 바뀔 때만 링 전체를 다시 그린다. setValue 는 늘어난 구간만
         * 갱신하므로, 색만 바꾸고 두면 이미 그려진 부분이 옛 색으로 남는다. */
        if (arcColor != rpmArcColor)
        {
            rpmArcColor = arcColor;
            rpmArcPainter.setColor(arcColor);
            rpmArc.invalidate();
        }
    }
    else
    {
        formatUnknown(rpmBuf, 10U);
        rpmText.setColor(rgb(DIM_R, DIM_G, DIM_B));
        rpmArc.setValue(0);
    }
    applyShiftLights(motorRpm, haveRpm);

    /* ---------------- SOC ---------------- */
    if (haveSoc)
    {
        /* 게이지에서는 소수점 이하가 의미 없다. 정수 퍼센트로 반올림해서
         * 숫자가 잘게 떨리지 않게 한다. */
        int32_t percent = (static_cast<int32_t>(socPerMille) + 5) / 10;

        touchgfx::Unicode::snprintf(socBuf, 10U, "%d",
                                    static_cast<int>(percent));
        socArc.setValue(static_cast<int>(socPerMille));

        if (socPerMille <= SOC_CRIT_PM)
        {
            socValue.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
            socArcPainter.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        }
        else if (socPerMille <= SOC_WARN_PM)
        {
            socValue.setColor(rgb(WARN_R, WARN_G, WARN_B));
            socArcPainter.setColor(rgb(WARN_R, WARN_G, WARN_B));
        }
        else
        {
            socValue.setColor(rgb(FG_R, FG_G, FG_B));
            socArcPainter.setColor(rgb(OK_R, OK_G, OK_B));
        }
    }
    else
    {
        formatUnknown(socBuf, 10U);
        socValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
        socArc.setValue(0);
    }

    /* ---------------- 온도 ---------------- */
    if (haveIgbt)
    {
        formatDeci(igbtBuf, 10U, igbtTempDecic);
        if (igbtTempDecic >= IGBT_CRIT_DECIC)
        {
            igbtValue.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        }
        else if (igbtTempDecic >= IGBT_WARN_DECIC)
        {
            igbtValue.setColor(rgb(WARN_R, WARN_G, WARN_B));
        }
        else
        {
            igbtValue.setColor(rgb(FG_R, FG_G, FG_B));
        }
    }
    else
    {
        formatUnknown(igbtBuf, 10U);
        igbtValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    if (haveCell)
    {
        formatDeci(cellBuf, 10U, cellTempDecic);
        if (cellTempDecic >= CELL_CRIT_DECIC)
        {
            cellValue.setColor(rgb(FAULT_R, FAULT_G, FAULT_B));
        }
        else if (cellTempDecic >= CELL_WARN_DECIC)
        {
            cellValue.setColor(rgb(WARN_R, WARN_G, WARN_B));
        }
        else
        {
            cellValue.setColor(rgb(FG_R, FG_G, FG_B));
        }
    }
    else
    {
        formatUnknown(cellBuf, 10U);
        cellValue.setColor(rgb(DIM_R, DIM_G, DIM_B));
    }

    rpmText.invalidate();
    socValue.invalidate();
    socArc.invalidate();
    igbtValue.invalidate();
    cellValue.invalidate();
}
