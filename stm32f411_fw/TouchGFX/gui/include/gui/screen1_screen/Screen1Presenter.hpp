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
    virtual void updateDash(int rpm, int motorTemp, int igbtTemp, int phaseCurrent, int soc, int vout);
    virtual ~Screen1Presenter() {}

private:
    Screen1Presenter();
    Screen1View& view;
};

#endif // SCREEN1PRESENTER_HPP