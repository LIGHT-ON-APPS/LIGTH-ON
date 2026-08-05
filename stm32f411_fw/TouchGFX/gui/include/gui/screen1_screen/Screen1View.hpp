#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextArea.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/canvas/Circle.hpp>
#include <touchgfx/widgets/canvas/PainterRGB565.hpp>
#include <touchgfx/containers/progress_indicators/CircleProgress.hpp>
#include <stdint.h>

/*
 * 주행 중인 드라이버가 보는 계기판.
 *
 * 화면 구성은 '힐끗 보고 읽는다'를 기준으로 정했다.
 *
 *  - 최상단 시프트 라이트 바: 주변시로 읽는 곳이다. 숫자를 읽으려면 시선을
 *    옮겨야 하지만 바는 눈을 코너에 두고도 색과 길이가 들어온다. 변속 판단은
 *    여기서 끝나야 한다.
 *  - 좌 RPM / 우 SOC 원형 게이지: 바늘 위치(=각도)는 숫자보다 빨리 읽힌다.
 *    정확한 값이 필요할 때만 가운데 숫자를 본다.
 *  - 하단 IGBT / CELL 온도: 주행 중 계속 볼 값이 아니다. 평소에는 조용한
 *    흰색이고 임계를 넘을 때만 색이 올라와 시선을 끈다.
 *
 * Designer(ViewBase)가 만든 위젯은 전부 숨기고 이 클래스가 화면을 직접 구성한다.
 * 표시 항목이 CAN/UART 로 들어오는 값에 따라 색과 자리표시자가 바뀌어야 하는데,
 * 그 규칙을 Designer 로 표현할 수 없기 때문이다. Designer 를 다시 열어도 이쪽
 * 레이아웃이 그대로 유지된다.
 */
class Screen1View : public Screen1ViewBase
{
public:
    /* 시프트 라이트 세그먼트 수. 폭 27 + 간격 3 으로 480 을 채운다. */
    static const uint8_t SHIFT_SEGMENTS = 16U;

    Screen1View();
    virtual ~Screen1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

    void updateApps(bool connected,
                    uint8_t flags,
                    uint16_t apps1Raw,
                    uint16_t apps2Raw,
                    uint16_t apps1PerMille,
                    uint16_t apps2PerMille,
                    uint16_t pedalPerMille,
                    int16_t command,
                    uint32_t status,
                    uint32_t fault);

    void updateTelemetry(bool fresh,
                         uint32_t validMask,
                         int32_t motorRpm,
                         uint16_t speedDkph,
                         uint16_t packDv,
                         int16_t packDa,
                         uint16_t socPerMille,
                         int16_t cellTempDecic,
                         int16_t igbtTempDecic);

protected:
    /* --- 배경 --- */
    touchgfx::Box screenBg;
    touchgfx::Box shiftRule;
    touchgfx::Box tempRule;
    touchgfx::Box centerRule;

    /* --- 시프트 라이트 --- */
    touchgfx::Box shiftSeg[SHIFT_SEGMENTS];

    /* --- RPM 게이지 ---
     * rpmText 라는 이름을 쓴 이유는 아래 온도 위젯 주석과 같다. ViewBase 에
     * rpmValue 가 이미 있어서, 같은 이름을 쓰면 setupScreen 의 setVisible(false)
     * 가 base 대신 이쪽 위젯을 숨겨 숫자가 통째로 사라진다. */
    touchgfx::Circle rpmTrack;
    touchgfx::PainterRGB565 rpmTrackPainter;
    touchgfx::CircleProgress rpmArc;
    touchgfx::PainterRGB565 rpmArcPainter;
    touchgfx::TextAreaWithOneWildcard rpmText;
    touchgfx::TextArea rpmLabel;

    /* --- SOC 게이지 --- */
    touchgfx::Circle socTrack2;
    touchgfx::PainterRGB565 socTrackPainter;
    touchgfx::CircleProgress socArc;
    touchgfx::PainterRGB565 socArcPainter;
    touchgfx::TextAreaWithOneWildcard socValue;
    touchgfx::TextArea socLabel;
    touchgfx::TextArea socPercent;

    /* --- 온도 --- */
    /* 이름을 base 와 겹치지 않게 둔다. Screen1ViewBase 에 igbtLabel/motorLabel 등이
     * 이미 있어서 같은 이름을 쓰면 파생 클래스 멤버가 base 를 가리고, setupScreen 의
     * setVisible(false) 가 엉뚱하게 이쪽 위젯을 숨긴다. */
    touchgfx::TextArea igbtTempLabel;
    touchgfx::TextAreaWithOneWildcard igbtValue;
    touchgfx::TextArea igbtUnit;
    touchgfx::TextArea cellTempLabel;
    touchgfx::TextAreaWithOneWildcard cellValue;
    touchgfx::TextArea cellUnit;

    /* --- 링크 상태 --- */
    touchgfx::Box linkDot;
    touchgfx::TextArea linkText;

    /* 와일드카드 버퍼. 서식이 넘치면 조용히 잘리므로 여유를 둔다. */
    touchgfx::Unicode::UnicodeChar rpmBuf[10];
    touchgfx::Unicode::UnicodeChar socBuf[10];
    touchgfx::Unicode::UnicodeChar igbtBuf[10];
    touchgfx::Unicode::UnicodeChar cellBuf[10];

private:
    void applyShiftLights(int32_t rpm, bool valid);
    void applyLinkState();

    bool appsFaultActive;
    bool linkConnected;

    /* 시프트 라이트 상태. 세그먼트 색을 매번 다시 칠하지 않고 바뀐 것만
     * invalidate 하려고 직전 색을 들고 있는다. 부분 프레임버퍼로 SPI 에 실어
     * 보내는 화면이라 불필요한 갱신이 그대로 프레임 시간이 된다. */
    touchgfx::colortype shiftColor[SHIFT_SEGMENTS];
    /* RPM 링의 현재 색. 같은 이유로 색이 실제로 바뀐 프레임에만 링을 통째로
     * 다시 그린다. */
    touchgfx::colortype rpmArcColor;
    bool shiftBlinkActive;
    bool shiftBlinkOn;
    uint8_t blinkTick;
    int32_t lastRpm;
    bool lastRpmValid;
};

#endif // SCREEN1VIEW_HPP
