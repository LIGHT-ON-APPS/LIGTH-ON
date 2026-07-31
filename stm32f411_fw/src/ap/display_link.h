#ifndef SRC_AP_DISPLAY_LINK_H_
#define SRC_AP_DISPLAY_LINK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw.h"
#include "display_protocol.h"


#define DISPLAY_LINK_UART_CH               _DEF_UART1
#define DISPLAY_LINK_UART_BAUD             115200U
#define DISPLAY_LINK_TIMEOUT_MS            300U


/*
 * 텔레메트리는 APPS 상태보다 느리게(10 Hz) 오고, 값이 갱신되지 않아도 주행에는
 * 지장이 없다. 그래서 링크 연결 판정(connected)은 APPS 패킷만으로 하고,
 * 텔레메트리는 자체 신선도만 따로 관리한다. 표시측은 이 값이 오래되면 숫자
 * 대신 자리표시자를 띄운다.
 */
#define DISPLAY_LINK_TELEMETRY_TIMEOUT_MS  1000U

typedef struct
{
  display_protocol_status_t status;
  uint32_t last_valid_ms;
  uint32_t valid_packet_count;
  uint32_t crc_error_count;
  uint32_t format_error_count;
  uint32_t sequence_gap_count;
  uint32_t duplicate_count;
  uint32_t stale_count;
  uint32_t uart_overrun_count;
  bool     connected;
  bool     has_data;

  display_protocol_telemetry_t telemetry;
  uint32_t telemetry_last_valid_ms;
  uint32_t telemetry_packet_count;
  uint32_t telemetry_gap_count;
  bool     telemetry_fresh;
  bool     has_telemetry;
} display_link_snapshot_t;


bool displayLinkInit(void);
void displayLinkUpdate(void);
bool displayLinkGetSnapshot(display_link_snapshot_t *p_snapshot);


#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_DISPLAY_LINK_H_ */
