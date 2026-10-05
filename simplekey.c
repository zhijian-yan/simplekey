// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

#include "simplekey.h"
#include <assert.h>
#include <string.h>

#define skey_check_param(param)     assert((param) != 0)
#define skey_is_pow2(param)         (!(param == 0 || param & (param - 1)))

// sig layer: 0-2 bits
#define SKEY_SIG_IDLE               0
#define SKEY_SIG_PRESS_DEBOUNCE     1
#define SKEY_SIG_PRESSED            2
#define SKEY_SIG_RELEASE_DEBOUNCE   3
#define SKEY_SIG_RELEASED           4
#define SKEY_SIG_SHIFT              0
#define SKEY_SIG_MASK               0x07U

// key layer: 3-4 bits
#define SKEY_KEY_IDLE               0
#define SKEY_KEY_PRESSED            1
#define SKEY_KEY_RELEASED           2
#define SKEY_KEY_SHIFT              3
#define SKEY_KEY_MASK               0x18U

// flag layer: 5-7 bits
#define SKEY_FLAG_LONG_PRESSED      0
#define SKEY_FLAG_MULTI_PRESSED     1
#define SKEY_FLAG_SHIFT             5

#define skey_state_get(shift, mask) ((uint8_t)((key->state & mask) >> shift))
#define skey_state_set(shift, mask, value) \
    (key->state = (uint8_t)((key->state & ~mask) | (value << shift)))

#define skey_flag_get(shift, flag) \
    ((uint8_t)(key->state & (1U << (flag + shift))))
#define skey_flag_set(shift, flag) \
    (key->state |= (uint8_t)(1U << (flag + shift)))
#define skey_flag_reset(shift, flag) \
    (key->state &= (uint8_t)~(1U << (flag + shift)))

#define skey_event_set(events, value) (events |= value)
#define skey_event_get(events, value) ((events) & value)

static int skey_queue_send(skey_queue_t *queue, const skey_message_t *message) {
    uint8_t w = queue->write_index;
    uint8_t next = (w + 1) & queue->capacity;
    if (next == queue->read_index)
        return 1;
    queue->buffer[w] = *message;
    queue->write_index = next;
    return 0;
}

static int skey_queue_receive(skey_queue_t *queue, skey_message_t *message) {
    uint8_t r = queue->read_index;
    if (r == queue->write_index)
        return 1;
    *message = queue->buffer[r];
    queue->read_index = (r + 1) & queue->capacity;
    return 0;
}

static uint8_t skey_scan_signal(skey_t *key, const skey_group_t *group,
                                uint8_t level) {
    uint8_t events = 0;
    for (;;) {
        switch (skey_state_get(SKEY_SIG_SHIFT, SKEY_SIG_MASK)) {
            case SKEY_SIG_IDLE:
                if (level == 0) {
                    key->ticks = 0;
                    skey_state_set(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                   SKEY_SIG_PRESS_DEBOUNCE);
                    skey_event_set(events, SKEY_EVENT_PRESS_EAGER);
                    continue;
                }
                break;
            case SKEY_SIG_PRESS_DEBOUNCE:
                if (key->ticks >= group->press_debounce_ticks) {
                    if (level == 0) {
                        key->ticks = 0;
                        skey_state_set(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                       SKEY_SIG_PRESSED);
                        skey_event_set(events, SKEY_EVENT_PRESS_DEFER);
                        continue;
                    } else
                        key->state = 0;
                } else
                    key->ticks += 1;
                break;
            case SKEY_SIG_PRESSED:
                if (level != 0) {
                    key->ticks = 0;
                    skey_state_set(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                   SKEY_SIG_RELEASE_DEBOUNCE);
                    skey_event_set(events, SKEY_EVENT_RELEASE_EAGER);
                    continue;
                }
                if (key->ticks < SKEY_MAX_TICK)
                    key->ticks += 1;
                break;
            case SKEY_SIG_RELEASE_DEBOUNCE:
                if (key->ticks >= group->release_debounce_ticks) {
                    if (level != 0) {
                        key->ticks = 0;
                        skey_state_set(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                       SKEY_SIG_RELEASED);
                        skey_event_set(events, SKEY_EVENT_RELEASE_DEFER);
                        continue;
                    } else
                        key->state = 0;
                } else
                    key->ticks += 1;
                break;
            case SKEY_SIG_RELEASED:
                if (level == 0) {
                    key->ticks = 0;
                    skey_state_set(SKEY_SIG_SHIFT, SKEY_SIG_MASK,
                                   SKEY_SIG_PRESS_DEBOUNCE);
                    skey_state_set(SKEY_KEY_SHIFT, SKEY_KEY_MASK,
                                   SKEY_KEY_IDLE);
                    skey_event_set(events, SKEY_EVENT_PRESS_EAGER);
                    continue;
                }
                if (key->ticks < SKEY_MAX_TICK)
                    key->ticks += 1;
        }
        break;
    }
    return events;
}

static uint8_t skey_gesture_proc(skey_t *key, const skey_group_t *group,
                                 uint8_t events) {
    for (;;) {
        switch (skey_state_get(SKEY_KEY_SHIFT, SKEY_KEY_MASK)) {
            case SKEY_KEY_IDLE:
                if ((group->press_debounce_mode == SKEY_DEBOUNCE_MODE_DEFER &&
                     skey_event_get(events, SKEY_EVENT_PRESS_DEFER)) ||
                    (group->press_debounce_mode == SKEY_DEBOUNCE_MODE_EAGER &&
                     skey_event_get(events, SKEY_EVENT_PRESS_EAGER))) {
                    skey_state_set(SKEY_KEY_SHIFT, SKEY_KEY_MASK,
                                   SKEY_KEY_PRESSED);
                    if (skey_flag_get(SKEY_FLAG_SHIFT,
                                      SKEY_FLAG_MULTI_PRESSED)) {
                        if (key->press_count < SKEY_MAX_COUNT)
                            key->press_count += 1;
                    } else {
                        key->press_count = 1;
                        skey_flag_set(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED);
                    }
                    continue;
                }
                break;
            case SKEY_KEY_PRESSED:
                if ((group->release_debounce_mode == SKEY_DEBOUNCE_MODE_DEFER &&
                     skey_event_get(events, SKEY_EVENT_RELEASE_DEFER)) ||
                    (group->release_debounce_mode == SKEY_DEBOUNCE_MODE_EAGER &&
                     skey_event_get(events, SKEY_EVENT_RELEASE_EAGER))) {
                    skey_state_set(SKEY_KEY_SHIFT, SKEY_KEY_MASK,
                                   SKEY_KEY_RELEASED);
                    continue;
                }
                // long press
                if (!skey_flag_get(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED)) {
                    if (key->ticks > group->long_press_expired_ticks) {
                        skey_flag_set(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED);
                        skey_event_set(events, SKEY_EVENT_LONG_PRESS);
                    }
                }
                // multi press
                if (skey_flag_get(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED)) {
                    if (key->ticks > group->multi_press_timeout_ticks) {
                        skey_flag_reset(SKEY_FLAG_SHIFT,
                                        SKEY_FLAG_MULTI_PRESSED);
                        skey_event_set(events, SKEY_EVENT_MULTI_PRESS_TIMEOUT);
                    }
                }
                break;
            case SKEY_KEY_RELEASED:
                // long press
                if (skey_flag_get(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED)) {
                    skey_flag_reset(SKEY_FLAG_SHIFT, SKEY_FLAG_LONG_PRESSED);
                    skey_event_set(events, SKEY_EVENT_LONG_RELEASE);
                }
                // multi press
                if (skey_flag_get(SKEY_FLAG_SHIFT, SKEY_FLAG_MULTI_PRESSED)) {
                    if (key->ticks > group->multi_release_timeout_ticks) {
                        skey_flag_reset(SKEY_FLAG_SHIFT,
                                        SKEY_FLAG_MULTI_PRESSED);
                        skey_event_set(events,
                                       SKEY_EVENT_MULTI_RELEASE_TIMEOUT);
                    }
                } else if (skey_state_get(SKEY_SIG_SHIFT, SKEY_SIG_MASK) ==
                           SKEY_SIG_RELEASED) {
                    key->state = 0;
                }
                break;
        }
        break;
    }
    return events;
}

void skey_init_key(skey_t *key, void *user_data) {
    skey_check_param(key);
    memset(key, 0, sizeof(skey_t));
    key->user_data = user_data;
}

void skey_init_group(skey_group_t *group, const skey_group_config_t *config) {
    skey_check_param(group);
    skey_check_param(config);
    skey_check_param(config->read_cb);
    skey_check_param(config->event_cb);
    if (config->callback_mode == SKEY_CALLBACK_MODE_DEFERRED) {
        skey_check_param(config->queue_buffer);
        skey_check_param(skey_is_pow2(config->queue_size));
        skey_check_param(config->queue_size <= SKEY_MAX_QUEUE_SIZE);
        skey_check_param(config->queue_size >= SKEY_MIN_QUEUE_SIZE);
    }
    memset(group, 0, sizeof(skey_group_t));
    group->read_cb = config->read_cb;
    group->event_cb = config->event_cb;
    group->callback_mode = config->callback_mode;
    group->queue.buffer = config->queue_buffer;
    group->queue.capacity = (uint8_t)(config->queue_size - 1);
    group->press_debounce_mode = config->press_debounce_mode;
    group->release_debounce_mode = config->release_debounce_mode;
    group->press_debounce_ticks = config->press_debounce_ticks;
    group->release_debounce_ticks = config->release_debounce_ticks;
    group->long_press_expired_ticks = config->long_press_expired_ticks;
    group->multi_press_timeout_ticks = config->multi_press_timeout_ticks;
    group->multi_release_timeout_ticks = config->multi_release_timeout_ticks;
}

int skey_scan(skey_t keys[], uint8_t key_count, skey_group_t *group) {
    skey_check_param(keys);
    skey_check_param(group);
    int ret = 0;
    while (key_count > 0) {
        key_count -= 1;
        uint8_t level = group->read_cb(keys[key_count].user_data);
        skey_message_t message;
        int lock_state = skey_lock();
        message.events = skey_scan_signal(&keys[key_count], group, level);
        message.events =
            skey_gesture_proc(&keys[key_count], group, message.events);
        message.press_count = keys[key_count].press_count;
        message.user_data = keys[key_count].user_data;
        skey_unlock(lock_state);
        if (message.events) {
            if (group->callback_mode == SKEY_CALLBACK_MODE_IMMEDIATE) {
                group->event_cb(message.events, message.press_count,
                                message.user_data);
            } else if (group->callback_mode == SKEY_CALLBACK_MODE_DEFERRED) {
                ret += skey_queue_send(&group->queue, &message);
            }
        }
    }
    return ret;
}

void skey_dispatch(uint8_t max_event_count, skey_group_t *group) {
    skey_check_param(group);
    skey_message_t message;
    while (max_event_count > 0 &&
           !skey_queue_receive(&group->queue, &message)) {
        max_event_count -= 1;
        group->event_cb(message.events, message.press_count, message.user_data);
    }
}
