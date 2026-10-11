#ifndef M5_MAX3421_IRQ_PROBE_H_
#define M5_MAX3421_IRQ_PROBE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void m5_max3421_irq_probe_snapshot(uint32_t *gpio_isr,
                                    uint32_t *sem_wake,
                                    uint32_t *low_service,
                                    uint32_t *handler,
                                    uint32_t *last_handler_ms);

#ifdef __cplusplus
}
#endif

#endif
