#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

#if !defined(SIMULATOR) && defined(__has_include)
#if __has_include("telemetry/telemetry.h")
#include "telemetry/telemetry.h"
#define GUI_HAS_TELEMETRY 1
#endif
#endif

#if defined(GUI_HAS_TELEMETRY)
namespace
{
int absInt(int value)
{
    return value < 0 ? -value : value;
}
}
#endif

Model::Model() : modelListener(0), tickCounter(0)
{
}

void Model::tick()
{
    tickCounter++;
    int phase = tickCounter % 600;
    int up = (phase < 300) ? phase : (600 - phase);

    int rpm = up * 6000 / 300 + 200;
    int motorTemp = 40 + up * 60 / 300;
    int igbtTemp = 35 + up * 50 / 300;
    int phaseCurrent = 10 + up * 180 / 300;
    int soc = 80 - up * 15 / 300;
    int vout = 240 + up * 180 / 300;

#if defined(GUI_HAS_TELEMETRY)
    telemetry_signal_snapshot_t snapshot;
    if (telemetryGetSignalSnapshot(&snapshot))
    {
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_RPM) != 0U)
        {
            rpm = absInt((int)snapshot.bamocar_rpm);
        }
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_MOTOR_TEMP_DECIC) != 0U)
        {
            motorTemp = (int)(snapshot.bamocar_motor_temp_decic / 10);
        }
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_IGBT_TEMP_DECIC) != 0U)
        {
            igbtTemp = (int)(snapshot.bamocar_igbt_temp_decic / 10);
        }
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_TORQUE_RAW) != 0U)
        {
            phaseCurrent = absInt((int)snapshot.bamocar_torque_raw);
        }
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BMS_SOC_PERMILLE) != 0U)
        {
            soc = (int)((snapshot.bms_soc_permille + 5U) / 10U);
        }
        if ((snapshot.valid_mask & TELEMETRY_SIGNAL_BAMOCAR_DC_BUS_MV) != 0U)
        {
            vout = (int)(snapshot.bamocar_dc_bus_mv / 1000L);
        }
    }
#endif

    if (modelListener != 0)
    {
        modelListener->updateDash(rpm, motorTemp, igbtTemp, phaseCurrent, soc, vout);
    }
}
