#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>
#include <stdint.h>

class ModelListener
{
public:
    ModelListener() : model(0) {}
    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }

    virtual void updateApps(bool connected,
                            uint8_t flags,
                            uint16_t apps1PerMille,
                            uint16_t apps2PerMille,
                            uint16_t pedalPerMille,
                            int16_t command,
                            uint32_t status,
                            uint32_t fault)
    {
        (void)connected;
        (void)flags;
        (void)apps1PerMille;
        (void)apps2PerMille;
        (void)pedalPerMille;
        (void)command;
        (void)status;
        (void)fault;
    }
protected:
    Model* model;
};

#endif // MODELLISTENER_HPP
