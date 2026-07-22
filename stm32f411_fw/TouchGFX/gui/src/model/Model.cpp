#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

#if !defined(SIMULATOR)
#include "display_link.h"
#endif

Model::Model()
    : modelListener(0),
      tickCounter(0),
      lastValidPacketCount(0U),
      lastConnected(false),
      linkStateInitialized(false)
{
}

void Model::tick()
{
    tickCounter++;

#if !defined(SIMULATOR)
    display_link_snapshot_t snapshot = {};
    bool hasData = displayLinkGetSnapshot(&snapshot);
    bool connected = hasData && snapshot.connected;
    bool packetChanged = hasData &&
                         (snapshot.valid_packet_count != lastValidPacketCount);

    if ((modelListener != 0) &&
        ((!linkStateInitialized) ||
         (connected != lastConnected) ||
         packetChanged))
    {
        modelListener->updateApps(connected,
                                  snapshot.status.flags,
                                  snapshot.status.apps1_per_mille,
                                  snapshot.status.apps2_per_mille,
                                  snapshot.status.pedal_per_mille,
                                  snapshot.status.command,
                                  snapshot.status.status,
                                  snapshot.status.fault);

        lastValidPacketCount = snapshot.valid_packet_count;
        lastConnected = connected;
        linkStateInitialized = true;
    }
#else
    int phase = tickCounter % 600;
    uint16_t pedal = static_cast<uint16_t>(
        ((phase < 300) ? phase : (600 - phase)) * 1000 / 300);
    if (modelListener != 0)
    {
        modelListener->updateApps(true,
                                  0x0FU,
                                  pedal,
                                  pedal,
                                  pedal,
                                  static_cast<int16_t>(pedal * 32767L / 1000L),
                                  0U,
                                  0U);
    }
#endif
}
