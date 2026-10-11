#ifndef MAX3421_HCD_PROBE_H_
#define MAX3421_HCD_PROBE_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t slot;
  uint8_t daddr;
  uint8_t ep_addr;
  uint8_t xfer_type;
  uint8_t interval;
  uint8_t state;
  uint8_t data_toggle;
  uint16_t packet_size;
  uint16_t total_len;
  uint16_t xferred_len;
  uint16_t frame_count;
  uint8_t controller_busy;
  uint8_t peraddr;
  uint8_t hxfr;
  uint8_t hirq;
  uint8_t hien;
  uint8_t last_hrsl;
  uint32_t submit_count;
  uint32_t hxfr_count;
  uint32_t hxfrdn_count;
  uint32_t nak_count;
  uint32_t success_count;
  uint32_t fail_count;
  uint32_t rcvdav_count;
} max3421_probe_snapshot_t;

typedef struct {
  uint8_t hirq;
  uint8_t hien;
  uint8_t hrsl;
  uint8_t hxfr;
  uint8_t mode;
  uint8_t usbirq;
  uint8_t usbien;
  uint8_t pinctl;
  uint8_t cpuctl;
} max3421_probe_registers_t;

// Observation-only snapshot. The slot is the internal endpoint table index.
// This function never submits, aborts, or otherwise changes a USB transfer.
bool tuh_max3421_probe_snapshot(uint8_t daddr, uint8_t slot,
                                max3421_probe_snapshot_t* snapshot);
uint8_t tuh_max3421_probe_slot_count(void);
bool tuh_max3421_probe_registers(uint8_t rhport,
                                 max3421_probe_registers_t* registers);

#ifdef __cplusplus
}
#endif

#endif
