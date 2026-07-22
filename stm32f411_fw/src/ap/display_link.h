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
} display_link_snapshot_t;


bool displayLinkInit(void);
void displayLinkUpdate(void);
bool displayLinkGetSnapshot(display_link_snapshot_t *p_snapshot);


#ifdef __cplusplus
}
#endif

#endif /* SRC_AP_DISPLAY_LINK_H_ */
