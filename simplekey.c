// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

#include "simplekey.h"
#include <assert.h>

#define SKEY_CHECK_PARAM(param) assert((param) != 0)
#define SKEY_CEHCK_IS_POW2(val) (!(val == 0 || val & (val - 1)))

#define SKEY_MAX_TICK (0xFFFF)
#define SKEY_MAX_COUNT (0xFF)

// sig layer: 0-2 bits
#define SKEY_SIG_IDLE 0
#define SKEY_SIG_PRESS_DEBOUNCE 1
#define SKEY_SIG_PRESSED 2
#define SKEY_SIG_RELEASE_DEBOUNCE 3
#define SKEY_SIG_RELEASED 4
#define SKEY_SIG_SHIFT 0
#define SKEY_SIG_MASK 0x07U

// key layer: 3-4 bits
#define SKEY_KEY_IDLE 0
#define SKEY_KEY_PRESSED 1
#define SKEY_KEY_RELEASED 2
#define SKEY_KEY_SHIFT 3
#define SKEY_KEY_MASK 0x18U

// flag layer: 5-7 bits
#define SKEY_FLAG_LONG_PRESSED 0
#define SKEY_FLAG_MULTI_PRESSED 1
#define SKEY_FLAG_SHIFT 5

#define SKEY_STATE_GET(shift, mask) ((uint8_t)((key->state & mask) >> shift))
#define SKEY_STATE_SET(shift, mask, value)                                     \
    (key->state = (uint8_t)((key->state & ~mask) | (value << shift)))

#define SKEY_FLAG_GET(shift, value)                                            \
    ((uint8_t)(key->state & (1U << (value + shift))))
#define SKEY_FLAG_SET(shift, value)                                            \
    (key->state |= (uint8_t)(1U << (value + shift)))
#define SKEY_FLAG_RESET(shift, value)                                          \
    (key->state &= (uint8_t)~(1U << (value + shift)))

static int skey_queue_send(skey_queue_t *queue, const skey_message_t *message) {
    uint8_t w = queue->write_index;
    uint8_t next = (w + 1) & (queue->length - 1);
    if (next == queue->read_index) {
        return 1;
    } else {
        queue->buffer[w] = *message;
        queue->write_index = next;
    }
    return 0;
}

static int skey_queue_receive(skey_queue_t *queue, skey_message_t *message) {
    uint8_t r = queue->read_index;
    if (r == queue->write_index) {
        return 1;
    } else {
        *message = queue->buffer[r];
        queue->read_index = (r + 1) & (queue->length - 1);
    }
    return 0;
}

static uint8_t skey_scan_signal(skey_t *key, const skey_config_t *config,
                                uint8_t level) {
    uint8_t event = 0;
    for (;;) {
        switch (SKEY_STATE_GET(SKEY_SIG_SHIFT, SKEY_SIG_MASK)) {
        case SKEY_SIG_IDLE:
            if (level == 0) {
                key->ticks = 0;
                SKEY_STATE_SET(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                               SKEY_SIG_PRESS_DEBOUNCE);
                SKEY_EVENT_SET(event, SKEY_EVENT_PRESS_EAGER);
                continue;
            }
            break;
        case SKEY_SIG_PRESS_DEBOUNCE:
            if (key->ticks >= config->press_debounce_ticks) {
                if (level == 0) {
                    key->ticks = 0;
                    SKEY_STATE_SET(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                   SKEY_SIG_PRESSED);
                    SKEY_EVENT_SET(event, SKEY_EVENT_PRESS_DEFER);
                    continue;
                } else
                    key->state = 0;
            } else
                key->ticks += 1;
            break;
        case SKEY_SIG_PRESSED:
            if (level != 0) {
                key->ticks = 0;
                SKEY_STATE_SET(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                               SKEY_SIG_RELEASE_DEBOUNCE);
                SKEY_EVENT_SET(event, SKEY_EVENT_RELEASE_EAGER);
                continue;
            }
            if (key->ticks < SKEY_MAX_TICK)
                key->ticks += 1;
            break;
        case SKEY_SIG_RELEASE_DEBOUNCE:
            if (key->ticks >= config->release_debounce_ticks) {
                if (level != 0) {
                    key->ticks = 0;
                    SKEY_STATE_SET(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                   SKEY_SIG_RELEASED);
                    SKEY_EVENT_SET(event, SKEY_EVENT_RELEASE_DEFER);
                    continue;
                } else
                    key->state = 0;
            } else
                key->ticks += 1;
            break;
        case SKEY_SIG_RELEASED:
            if (level == 0) {
                key->ticks = 0;
                SKEY_STATE_SET(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                               SKEY_SIG_PRESS_DEBOUNCE);
                SKEY_STATE_SET(SKEY_KEY_SHIFT, SKEY_KEY_MASK, SKEY_KEY_IDLE);
                SKEY_EVENT_SET(event, SKEY_EVENT_PRESS_EAGER);
                continue;
            }
            if (key->ticks < SKEY_MAX_TICK)
                key->ticks += 1;
        }
        break;
    }
    return event;
}

static uint8_t skey_gesture_proc(skey_t *key, const skey_config_t *config,
                                 uint8_t event) {
    for (;;) {
        switch (SKEY_STATE_GET(SKEY_KEY_SHIFT, SKEY_KEY_MASK)) {
        case SKEY_KEY_IDLE:
            if ((config->press_db_mode == SKEY_DEBOUNCE_MODE_DEFER &&
                 SKEY_EVENT_GET(event, SKEY_EVENT_PRESS_DEFER)) ||
                (config->press_db_mode == SKEY_DEBOUNCE_MODE_EAGER &&
                 SKEY_EVENT_GET(event, SKEY_EVENT_PRESS_EAGER))) {
                SKEY_STATE_SET(SKEY_KEY_SHIFT, SKEY_KEY_MASK, SKEY_KEY_PRESSED);
                if (SKEY_FLAG_GET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED)) {
                    if (key->press_count < SKEY_MAX_COUNT)
                        key->press_count += 1;
                } else {
                    key->press_count = 1;
                    SKEY_FLAG_SET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED);
                }
                continue;
            }
            break;
        case SKEY_KEY_PRESSED:
            if ((config->release_db_mode == SKEY_DEBOUNCE_MODE_DEFER &&
                 SKEY_EVENT_GET(event, SKEY_EVENT_RELEASE_DEFER)) ||
                (config->release_db_mode == SKEY_DEBOUNCE_MODE_EAGER &&
                 SKEY_EVENT_GET(event, SKEY_EVENT_RELEASE_EAGER))) {
                SKEY_STATE_SET(SKEY_KEY_SHIFT, SKEY_KEY_MASK,
                               SKEY_KEY_RELEASED);
                continue;
            }
            // long press
            if (!SKEY_FLAG_GET(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED)) {
                if (key->ticks > config->long_press_expired_ticks) {
                    SKEY_FLAG_SET(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED);
                    SKEY_EVENT_SET(event, SKEY_EVENT_LONG_PRESS);
                }
            }
            // multi press
            if (SKEY_FLAG_GET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED)) {
                if (key->ticks > config->multi_press_timeout_ticks) {
                    SKEY_FLAG_RESET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED);
                    SKEY_EVENT_SET(event, SKEY_EVENT_MULTI_PRESS_TIMEOUT);
                }
            }
            break;
        case SKEY_KEY_RELEASED:
            // long press
            if (SKEY_FLAG_GET(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED)) {
                SKEY_FLAG_RESET(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED);
                SKEY_EVENT_SET(event, SKEY_EVENT_LONG_RELEASE);
            }
            // multi press
            if (SKEY_FLAG_GET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED)) {
                if (key->ticks > config->multi_release_timeout_ticks) {
                    SKEY_FLAG_RESET(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED);
                    SKEY_EVENT_SET(event, SKEY_EVENT_MULTI_RELEASE_TIMEOUT);
                }
            } else if (SKEY_STATE_GET(SKEY_SIG_SHIFT, SKEY_SIG_MASK) ==
                       SKEY_SIG_RELEASED) {
                key->state = 0;
            }
            break;
        }
        break;
    }
    return event;
}

uint8_t skey_scan(skey_t keys[], uint8_t key_num, skey_config_t *config) {
    SKEY_CHECK_PARAM(keys);
    SKEY_CHECK_PARAM(config);
    SKEY_CHECK_PARAM(config->read_cb);
    SKEY_CHECK_PARAM(config->event_cb);
    uint8_t ret = 0;
    uint8_t level;
    int skey_lock_state;
    skey_message_t message;
    while (key_num > 0) {
        --key_num;
        level = config->read_cb(keys[key_num].user_data);
        skey_lock_state = skey_lock();
        message.event = skey_scan_signal(&keys[key_num], config, level);
        message.event =
            skey_gesture_proc(&keys[key_num], config, message.event);
        message.press_count = keys[key_num].press_count;
        message.user_data = keys[key_num].user_data;
        skey_unlock(skey_lock_state);
        if (message.event) {
            if (config->cb_mode == SKEY_CALLBACK_MODE_IMMEDIATE) {
                config->event_cb(message.event, message.press_count,
                                 message.user_data);
            } else if (config->queue.buffer) {
                SKEY_CHECK_PARAM(SKEY_CEHCK_IS_POW2(config->queue.length));
                ret += skey_queue_send(&config->queue, &message);
            }
        }
    }
    return ret;
}

void skey_dispatch(uint8_t max_event_num, skey_config_t *config) {
    SKEY_CHECK_PARAM(config);
    skey_message_t message;
    while (max_event_num > 0 && !skey_queue_receive(&config->queue, &message)) {
        --max_event_num;
        config->event_cb(message.event, message.press_count, message.user_data);
    }
}
