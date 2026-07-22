#ifndef SCREEN1PRESENTER_HPP
#define SCREEN1PRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class Screen1View;

class Screen1Presenter : public touchgfx::Presenter, public ModelListener
{
public:
    Screen1Presenter(Screen1View& v);
    virtual void activate();
    virtual void deactivate();
    virtual void updateApps(bool connected,
                            uint8_t flags,
                            uint16_t apps1PerMille,
                            uint16_t apps2PerMille,
                            uint16_t pedalPerMille,
                            int16_t command,
                            uint32_t status,
                            uint32_t fault);
    virtual ~Screen1Presenter() {}

private:
    Screen1Presenter();
    Screen1View& view;
};

#endif // SCREEN1PRESENTER_HPP
