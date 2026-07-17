#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>

class ModelListener
{
public:
    ModelListener() : model(0) {}
    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }

    virtual void updateDash(int rpm, int motorTemp, int igbtTemp, int phaseCurrent, int soc, int vout)
    {
        (void)rpm;
        (void)motorTemp;
        (void)igbtTemp;
        (void)phaseCurrent;
        (void)soc;
        (void)vout;
    }
protected:
    Model* model;
};

#endif // MODELLISTENER_HPP