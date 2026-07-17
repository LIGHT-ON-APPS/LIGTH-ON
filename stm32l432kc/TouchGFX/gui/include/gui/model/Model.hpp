#ifndef MODEL_HPP
#define MODEL_HPP

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

    // A-stage demo: animates dashboard values in the simulator.
    // Replace with real CAN / data_logger values during hardware integration (B-stage).
    int tickCounter;
};

#endif // MODEL_HPP
