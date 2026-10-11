#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core2-specific source translated into generic GAME_INPUT_* flags.
uint32_t core2_input_source_poll(void);

#ifdef __cplusplus
}
#endif
