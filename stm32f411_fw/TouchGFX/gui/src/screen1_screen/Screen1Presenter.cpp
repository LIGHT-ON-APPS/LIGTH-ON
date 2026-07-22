#include <gui/screen1_screen/Screen1View.hpp>
#include <gui/screen1_screen/Screen1Presenter.hpp>

Screen1Presenter::Screen1Presenter(Screen1View& v)
    : view(v)
{
}

void Screen1Presenter::activate()
{
}

void Screen1Presenter::deactivate()
{
}

void Screen1Presenter::updateApps(bool connected,
                                  uint8_t flags,
                                  uint16_t apps1PerMille,
                                  uint16_t apps2PerMille,
                                  uint16_t pedalPerMille,
                                  int16_t command,
                                  uint32_t status,
                                  uint32_t fault)
{
    view.updateApps(connected,
                    flags,
                    apps1PerMille,
                    apps2PerMille,
                    pedalPerMille,
                    command,
                    status,
                    fault);
}
