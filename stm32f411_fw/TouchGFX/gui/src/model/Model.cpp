#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>

#if !defined(SIMULATOR)
#include "display_link.h"
#else
#include "display_protocol.h"
#endif

Model::Model()
    : modelListener(0),
      tickCounter(0),
      lastValidPacketCount(0U),
      lastTelemetryPacketCount(0U),
      lastConnected(false),
      lastTelemetryFresh(false),
      linkStateInitialized(false),
      telemetryStateInitialized(false)
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

    if (modelListener == 0)
    {
        return;
    }

    if ((!linkStateInitialized) ||
        (connected != lastConnected) ||
        packetChanged)
    {
        modelListener->updateApps(connected,
                                  snapshot.status.flags,
                                  snapshot.status.apps1_raw,
                                  snapshot.status.apps2_raw,
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

    /*
     * 텔레메트리는 APPS 와 갱신 주기도 유실 조건도 다르므로 따로 판정한다.
     * 새 패킷이 왔을 때와, 값은 그대로인데 신선도만 떨어졌을 때 모두 화면을
     * 갱신해야 한다. 후자를 빠뜨리면 CAN 이 끊긴 뒤에도 마지막 숫자가 계속
     * 정상값처럼 남는다.
     */
    {
        bool telemetryChanged =
            snapshot.telemetry_packet_count != lastTelemetryPacketCount;

        if ((!telemetryStateInitialized) ||
            telemetryChanged ||
            (snapshot.telemetry_fresh != lastTelemetryFresh))
        {
            modelListener->updateTelemetry(
                snapshot.telemetry_fresh,
                snapshot.telemetry.valid_mask,
                snapshot.telemetry.motor_rpm,
                snapshot.telemetry.road_speed_dkph,
                snapshot.telemetry.pack_dv,
                snapshot.telemetry.pack_da,
                snapshot.telemetry.soc_per_mille,
                snapshot.telemetry.cell_max_temp_decic,
                snapshot.telemetry.igbt_temp_decic);

            lastTelemetryPacketCount = snapshot.telemetry_packet_count;
            lastTelemetryFresh = snapshot.telemetry_fresh;
            telemetryStateInitialized = true;
        }
    }
#else
    /* 시뮬레이터: 화면 배치와 색 전환을 보드 없이 확인하기 위한 합성 신호.
     * 페달을 훑으면서 온도를 정상 -> 경고 -> 위험까지 올린다. */
    int phase = tickCounter % 600;
    uint16_t pedal = static_cast<uint16_t>(
        ((phase < 300) ? phase : (600 - phase)) * 1000 / 300);

    if (modelListener != 0)
    {
        modelListener->updateApps(true,
                                  0x0FU,
                                  static_cast<uint16_t>(pedal * 4U),
                                  static_cast<uint16_t>(pedal * 4U + 3U),
                                  pedal,
                                  pedal,
                                  pedal,
                                  static_cast<int16_t>(pedal * 32767L / 1000L),
                                  0U,
                                  0U);

        modelListener->updateTelemetry(
            true,
            DISPLAY_PROTOCOL_TLM_PACK_V | DISPLAY_PROTOCOL_TLM_PACK_A |
                DISPLAY_PROTOCOL_TLM_SOC | DISPLAY_PROTOCOL_TLM_CELL_TEMP |
                DISPLAY_PROTOCOL_TLM_RPM | DISPLAY_PROTOCOL_TLM_IGBT_TEMP,
            static_cast<int32_t>(pedal * 6500L / 1000L),
            0U,
            3962U,
            static_cast<int16_t>(pedal * 2000L / 1000L),
            784U,
            static_cast<int16_t>(300 + (pedal * 300L / 1000L)),
            static_cast<int16_t>(350 + (pedal * 500L / 1000L)));
    }
#endif
}
