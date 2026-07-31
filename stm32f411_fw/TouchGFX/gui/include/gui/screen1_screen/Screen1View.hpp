#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextArea.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <stdint.h>

/*
 * 주행 중 어두운 환경에서 운전자가 힐끗 보는 계기판.
 *
 * Designer(ViewBase)가 만든 위젯은 전부 숨기고 이 클래스가 화면을 직접 구성한다.
 * 표시 항목이 CAN/UART 로 들어오는 값에 따라 색과 자리표시자가 바뀌어야 하는데,
 * 그 규칙을 Designer 로 표현할 수 없기 때문이다. Designer 를 다시 열어도 이쪽
 * 레이아웃이 그대로 유지된다.
 */
class Screen1View : public Screen1ViewBase
{
public:
    Screen1View();
    virtual ~Screen1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

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
    /* --- 배경/패널 --- */
    touchgfx::Box screenBg;
    touchgfx::Box headerRule;
    touchgfx::Box footerRule;
    touchgfx::Box cellTile;
    touchgfx::Box igbtTile;
    touchgfx::Box cellAccent;
    touchgfx::Box igbtAccent;

    /* --- 상태 표시 --- */
    touchgfx::Box linkDot;
    touchgfx::TextArea linkText;

    /* --- 주 수치 --- */
    touchgfx::TextAreaWithOneWildcard rpmText;
    touchgfx::TextArea rpmUnitText;
    touchgfx::TextAreaWithOneWildcard speedText;
    touchgfx::TextArea speedUnitText;

    /* --- 온도 타일 --- */
    /* 이름을 base 와 겹치지 않게 둔다. Screen1ViewBase 에 igbtLabel/motorLabel 등이
     * 이미 있어서 같은 이름을 쓰면 파생 클래스 멤버가 base 를 가리고, setupScreen 의
     * setVisible(false) 가 엉뚱하게 이쪽 위젯을 숨긴다. */
    touchgfx::TextArea cellTempLabel;
    touchgfx::TextAreaWithOneWildcard cellValue;
    touchgfx::TextArea igbtTempLabel;
    touchgfx::TextAreaWithOneWildcard igbtValue;

    /* --- SOC --- */
    touchgfx::TextArea socLabel;
    touchgfx::TextAreaWithOneWildcard socValue;
    touchgfx::TextArea socPercent;

    /* --- 페달 / 아날로그 입력 --- */
    touchgfx::TextArea pedalLabel;
    touchgfx::Box pedalTrack;
    touchgfx::Box pedalFillBar;
    touchgfx::TextAreaWithOneWildcard pedalValue;
    touchgfx::TextArea pedalPercent;

    touchgfx::TextArea ain1Label;
    touchgfx::Box ain1Track;
    touchgfx::Box ain1FillBar;
    touchgfx::TextAreaWithOneWildcard ain1Value;

    touchgfx::TextArea ain2Label;
    touchgfx::Box ain2Track;
    touchgfx::Box ain2FillBar;
    touchgfx::TextAreaWithOneWildcard ain2Value;

    /* --- 팩 전압/전류 --- */
    touchgfx::TextArea packLabel;
    touchgfx::TextAreaWithOneWildcard packValue;
    touchgfx::TextArea packUnit;
    touchgfx::TextArea ampLabel;
    touchgfx::TextAreaWithOneWildcard ampValue;
    touchgfx::TextArea ampUnit;

    /* 와일드카드 버퍼. 서식이 넘치면 조용히 잘리므로 여유를 둔다. */
    touchgfx::Unicode::UnicodeChar rpmBuf[10];
    touchgfx::Unicode::UnicodeChar speedBuf[10];
    touchgfx::Unicode::UnicodeChar cellBuf[10];
    touchgfx::Unicode::UnicodeChar igbtBuf[10];
    touchgfx::Unicode::UnicodeChar socBuf[10];
    touchgfx::Unicode::UnicodeChar pedalBuf[10];
    touchgfx::Unicode::UnicodeChar ain1Buf[10];
    touchgfx::Unicode::UnicodeChar ain2Buf[10];
    touchgfx::Unicode::UnicodeChar packBuf[10];
    touchgfx::Unicode::UnicodeChar ampBuf[10];

private:
    bool appsFaultActive;
    bool linkConnected;
};

#endif // SCREEN1VIEW_HPP
