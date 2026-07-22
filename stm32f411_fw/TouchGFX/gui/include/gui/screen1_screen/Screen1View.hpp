#ifndef SCREEN1VIEW_HPP
#define SCREEN1VIEW_HPP

#include <gui_generated/screen1_screen/Screen1ViewBase.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <stdint.h>

class Screen1View : public Screen1ViewBase
{
public:
    Screen1View();
    virtual ~Screen1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void updateApps(bool connected,
                    uint8_t flags,
                    uint16_t apps1PerMille,
                    uint16_t apps2PerMille,
                    uint16_t pedalPerMille,
                    int16_t command,
                    uint32_t status,
                    uint32_t fault);
protected:
    touchgfx::TextAreaWithOneWildcard pedalText;
    touchgfx::TextAreaWithOneWildcard apps1Text;
    touchgfx::TextAreaWithOneWildcard apps2Text;
    touchgfx::TextAreaWithOneWildcard commandText;
    touchgfx::TextAreaWithOneWildcard faultText;

    touchgfx::Unicode::UnicodeChar pedalBuffer[8];
    touchgfx::Unicode::UnicodeChar apps1Buffer[8];
    touchgfx::Unicode::UnicodeChar apps2Buffer[8];
    touchgfx::Unicode::UnicodeChar commandBuffer[8];
    touchgfx::Unicode::UnicodeChar faultBuffer[11];
};

#endif // SCREEN1VIEW_HPP
