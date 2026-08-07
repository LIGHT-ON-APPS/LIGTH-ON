#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/BoxWithBorder.hpp>
#include <touchgfx/widgets/TextArea.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/canvas/Circle.hpp>
#include <touchgfx/widgets/canvas/PainterRGB565.hpp>
#include <touchgfx/containers/progress_indicators/BoxProgress.hpp>
#include <touchgfx/containers/progress_indicators/CircleProgress.hpp>
#include <stdint.h>

/**
 * Compact 480x320 motorsport display.
 *
 * The Designer-generated widgets are retained for project compatibility, but
 * the runtime dashboard is composed here so live colors and safety state can
 * be updated without regenerating the Designer project.
 */
class Screen1View : public Screen1ViewBase
{
public:
    static const uint8_t SHIFT_SEGMENTS = 16U;

    /*
     * CanvasWidgetRenderer 작업 버퍼.
     *
     * Designer 가 만든 ViewBase 는 4800 바이트로 잡아 두는데, 이 화면의 링은 폭이
     * 168 px 이라 그 버퍼로는 한 번에 다 못 그리고 위젯 하나를 여러 띠로 나눠
     * 반복해서 그리게 된다. 같은 그림을 여러 번 그리는 셈이라 프레임을 그대로
     * 깎아먹는다. 생성자에서 이 버퍼로 다시 잡아 준다.
     */
    static const uint32_t CANVAS_BUFFER_SIZE = 16000U;

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
                         int16_t igbtTempDecic,
                         int16_t motorTempDecic);

protected:
    touchgfx::Box screenBg;

    /* Shift-light housing and LEDs. */
    touchgfx::BoxWithBorder shiftHousing;
    touchgfx::Box shiftSeg[SHIFT_SEGMENTS];

    /* Left battery gauge. */
    touchgfx::BoxWithBorder socBatteryBody;
    touchgfx::Box socBatteryTerminal;
    touchgfx::BoxProgress socBatteryFill;
    touchgfx::TextAreaWithOneWildcard socValue;
    touchgfx::TextArea socLabel;

    /* Right speed gauge. */
    touchgfx::Circle speedTrack;
    touchgfx::PainterRGB565 speedTrackPainter;
    touchgfx::CircleProgress speedArc;
    touchgfx::PainterRGB565 speedArcPainter;
    touchgfx::TextAreaWithOneWildcard speedValue;
    touchgfx::TextArea speedLabel;
    touchgfx::TextArea speedUnit;

    /* Central RPM and system-state module. */
    touchgfx::BoxWithBorder centerPanel;
    touchgfx::TextAreaWithOneWildcard rpmText;
    touchgfx::TextArea rpmLabel;
    touchgfx::BoxWithBorder statusPanel;
    touchgfx::Box statusAccent;
    touchgfx::TextArea statusText;

    /* Bottom temperature tiles. Initials and units are static; only the large
     * numeric widget is invalidated when telemetry changes. */
    touchgfx::BoxWithBorder motorPanel;
    touchgfx::TextArea motorTempLabel;
    touchgfx::TextAreaWithOneWildcard motorValue;
    touchgfx::TextArea motorUnit;

    touchgfx::BoxWithBorder igbtPanel;
    touchgfx::TextArea igbtTempLabel;
    touchgfx::TextAreaWithOneWildcard igbtValue;
    touchgfx::TextArea igbtUnit;

    touchgfx::BoxWithBorder cellPanel;
    touchgfx::TextArea cellTempLabel;
    touchgfx::TextAreaWithOneWildcard cellValue;
    touchgfx::TextArea cellUnit;

    touchgfx::Unicode::UnicodeChar rpmBuf[10];
    touchgfx::Unicode::UnicodeChar socBuf[10];
    touchgfx::Unicode::UnicodeChar speedBuf[10];
    touchgfx::Unicode::UnicodeChar motorBuf[10];
    touchgfx::Unicode::UnicodeChar igbtBuf[10];
    touchgfx::Unicode::UnicodeChar cellBuf[10];

private:
    /*
     * 마지막으로 화면에 그려 넣은 값. 텔레메트리는 같은 값이 반복해서 들어오는
     * 일이 흔한데, 그걸 그대로 invalidate 로 옮기면 F411 이 SPI 로 화면을 밀어내는
     * 속도를 넘겨서 갱신이 눈에 띄게 끊긴다. 값도 색도 그대로면 다시 그리지 않는다.
     */
    struct DrawnField
    {
        int32_t value;
        touchgfx::colortype color;
        /* 온도 타일처럼 배경(color)과 글자색이 따로 노는 경우에 쓴다. 배경이
         * 그대로여도 글자색만 바뀌는 전환(값 없음 -> 정상)이 있어서, 이걸
         * 같이 보지 않으면 글자가 흐린 색 그대로 남는다. */
        touchgfx::colortype ink;
        bool valid;
        bool drawn;
    };

    static bool fieldChanged(DrawnField& field,
                             int32_t value,
                             bool valid,
                             touchgfx::colortype color);

    void applyShiftLights(int32_t rpm, bool valid);
    void applyLinkState();

    /* 온도 타일 하나를 갱신한다. 세 타일이 규칙이 같아서 한 곳으로 묶었다. */
    void updateTempTile(DrawnField& drawn,
                        touchgfx::BoxWithBorder& panel,
                        touchgfx::TextArea& initial,
                        touchgfx::TextAreaWithOneWildcard& value,
                        touchgfx::TextArea& unit,
                        touchgfx::Unicode::UnicodeChar* buffer,
                        int16_t decic,
                        bool valid,
                        int16_t warnDecic,
                        int16_t critDecic);

    bool appsFaultActive;
    bool linkConnected;

    touchgfx::colortype shiftColor[SHIFT_SEGMENTS];
    touchgfx::colortype socFillColor;
    touchgfx::colortype speedArcColor;
    bool shiftBlinkActive;
    bool shiftBlinkOn;
    uint8_t blinkTick;
    int32_t lastRpm;
    bool lastRpmValid;

    DrawnField rpmDrawn;
    DrawnField socDrawn;
    DrawnField speedDrawn;
    DrawnField motorDrawn;
    DrawnField igbtDrawn;
    DrawnField cellDrawn;

    /* 링크 상태도 전이할 때만 다시 그린다. 배지 3개(패널/밑줄/글자)가 매
     * APPS 패킷마다 갱신되면 그것만으로 프레임을 잡아먹는다. */
    bool linkStateDrawn;
    bool drawnConnected;
    bool drawnFaultActive;

    uint8_t canvasBuffer[CANVAS_BUFFER_SIZE];
};

#endif // SCREEN1VIEW_HPP
