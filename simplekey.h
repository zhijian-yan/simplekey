// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

#ifndef SIMPLEKEY_H
#define SIMPLEKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

static inline int skey_lock(void) {
    /* Disable interrupts if needed */
    return 0;
}

static inline void skey_unlock(int skey_lock_state) {
    /* Restore interrupt state */
    (void)skey_lock_state;
}

#define SKEY_EVENT_PRESS_DEFER (1U << 0)
#define SKEY_EVENT_PRESS_EAGER (1U << 1)
#define SKEY_EVENT_RELEASE_DEFER (1U << 2)
#define SKEY_EVENT_RELEASE_EAGER (1U << 3)
#define SKEY_EVENT_LONG_PRESS (1U << 4)
#define SKEY_EVENT_LONG_RELEASE (1U << 5)
#define SKEY_EVENT_MULTI_PRESS_TIMEOUT (1U << 6)
#define SKEY_EVENT_MULTI_RELEASE_TIMEOUT (1U << 7)

#define SKEY_EVENT_SET(event, value) (event |= value)
#define SKEY_EVENT_GET(event, value) ((event) & value)

typedef enum {
    SKEY_CALLBACK_MODE_DEFERRED = 0,
    SKEY_CALLBACK_MODE_IMMEDIATE,
} skey_cb_mode_t;

typedef enum {
    SKEY_DEBOUNCE_MODE_DEFER = 0,
    SKEY_DEBOUNCE_MODE_EAGER,
} skey_db_mode_t;

typedef struct {
    volatile uint16_t ticks;
    volatile uint8_t press_count;
    volatile uint8_t state;
    void *user_data;
} skey_t;

typedef struct {
    uint8_t event;
    uint8_t press_count;
    void *user_data;
} skey_message_t;

typedef struct {
    skey_message_t *buffer;
    uint8_t length;
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;

typedef struct {
    uint8_t (*read_cb)(void *user_data);
    void (*event_cb)(uint8_t event, uint8_t press_count, void *user_data);
    skey_cb_mode_t cb_mode;
    skey_db_mode_t press_db_mode;
    skey_db_mode_t release_db_mode;
    uint16_t press_debounce_ticks;
    uint16_t release_debounce_ticks;
    uint16_t long_press_expired_ticks;
    uint16_t multi_press_timeout_ticks;
    uint16_t multi_release_timeout_ticks;
    skey_queue_t queue;
} skey_config_t;

uint8_t skey_scan(skey_t keys[], uint8_t key_num, skey_config_t *config);
void skey_dispatch(uint8_t max_event_num, skey_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
