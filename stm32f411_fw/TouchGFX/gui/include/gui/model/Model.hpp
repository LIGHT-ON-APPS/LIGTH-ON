#ifndef MODEL_HPP
#define MODEL_HPP

#include <stdint.h>

class ModelListener;

class Model
{
public:
    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();
protected:
    ModelListener* modelListener;
    uint32_t tickCounter;
    uint32_t lastValidPacketCount;
    bool lastConnected;
    bool linkStateInitialized;
};

#endif // MODEL_HPP
