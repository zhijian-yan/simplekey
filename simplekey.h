// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

/**
 * @file
 * @brief Public interface of simplekey.
 *
 * Declares the key object, the group that holds the callbacks and the
 * configuration shared by a set of keys, and the event queue a group uses. The
 * platform hooks live in simplekey_port.h.
 *
 * A group is set up with skey_init_group() and its keys with skey_init_key();
 * the keys are then driven by calling skey_scan() periodically and
 * skey_dispatch() to run the callbacks.
 */

#ifndef SIMPLEKEY_H
#define SIMPLEKEY_H

#include <stdint.h>

/**
 * @brief Platform port header.
 *
 * Define this macro to the name of the header providing the platform hooks
 * (skey_lock() and skey_unlock()) when the port file lives outside the library
 * tree, for example -DSKEY_PORT_HEADER='"my_port.h"'. When it is not defined,
 * the shipped "simplekey_port.h" next to this header is used.
 */
#ifdef SKEY_PORT_HEADER
#include SKEY_PORT_HEADER
#else
#include "simplekey_port.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum value of the internal tick counters.
 *
 * skey_t.ticks stops increasing once it reaches this value.
 */
#define SKEY_MAX_TICK                    (0xFFFF)
/**
 * @brief Maximum value of the press counter.
 *
 * skey_t.press_count stops increasing once it reaches this value, so a longer
 * click sequence is still reported as SKEY_MAX_COUNT presses.
 */
#define SKEY_MAX_COUNT                   (0xFF)
/**
 * @brief Maximum number of elements of a queue buffer.
 *
 * @note The ring buffer always leaves one slot free, so a queue created with
 *       this size holds at most 255 messages.
 */
#define SKEY_MAX_QUEUE_SIZE              (256)
/**
 * @brief Minimum number of elements of a queue buffer.
 *
 * @note A queue created with this size holds at most one message.
 */
#define SKEY_MIN_QUEUE_SIZE              (2)

/** Press debounce confirmed; the key is considered pressed. */
#define SKEY_EVENT_PRESS_DEFER           (1U << 0)
/** Level changed to pressed, before the debounce is confirmed. */
#define SKEY_EVENT_PRESS_EAGER           (1U << 1)
/** Release debounce confirmed; the key is considered released. */
#define SKEY_EVENT_RELEASE_DEFER         (1U << 2)
/** Level changed to released, before the debounce is confirmed. */
#define SKEY_EVENT_RELEASE_EAGER         (1U << 3)
/** The key stayed pressed for longer than long_press_expired_ticks. */
#define SKEY_EVENT_LONG_PRESS            (1U << 4)
/** The key was released after a long press. */
#define SKEY_EVENT_LONG_RELEASE          (1U << 5)
/** Multi-press wait while pressed exceeded multi_press_timeout_ticks. */
#define SKEY_EVENT_MULTI_PRESS_TIMEOUT   (1U << 6)
/** Multi-press wait while released exceeded multi_release_timeout_ticks. */
#define SKEY_EVENT_MULTI_RELEASE_TIMEOUT (1U << 7)

/**
 * @brief Callback execution mode.
 *
 * Selects when the event callback runs.
 */
typedef enum {
    /** Events are queued and executed later by skey_dispatch(). */
    SKEY_CALLBACK_MODE_DEFERRED = 0,
    /** The callback runs in skey_scan() as soon as an event is raised. */
    SKEY_CALLBACK_MODE_IMMEDIATE,
} skey_callback_mode_t;

/**
 * @brief Debounce mode.
 *
 * Selects which of the events that the signal layer emits for one level change
 * the gesture layer reacts to. Press and release debouncing are configured
 * independently.
 */
typedef enum {
    /** React after the debounce is confirmed; more bounce-resistant. */
    SKEY_DEBOUNCE_MODE_DEFER = 0,
    /** React immediately on the level change; lower latency, less filtering. */
    SKEY_DEBOUNCE_MODE_EAGER,
} skey_debounce_mode_t;

/**
 * @brief Key object.
 *
 * Create it with skey_init_key(). Apart from user_data, every field is internal
 * state maintained by skey_scan() and must be treated as read-only by the
 * application.
 */
typedef struct {
    /**
     * Internal tick counter driving the debounce, long-press and timeout
     * decisions; managed by skey_scan().
     */
    volatile uint16_t ticks;
    /**
     * Number of presses within the current click sequence, reported to the
     * event callback; managed by skey_scan().
     */
    volatile uint8_t press_count;
    /** Internal packed signal state, gesture state and flags. */
    volatile uint8_t state;
    /** User data passed to read_cb and event_cb for this key. */
    void *user_data;
} skey_t;

/**
 * @brief Event queue message.
 *
 * One entry of a group's event queue, describing an event raised by skey_scan()
 * and not yet delivered by skey_dispatch().
 */
typedef struct {
    /** Bitwise OR of the SKEY_EVENT_* flags raised by the same scan. */
    uint8_t events;
    /** Number of presses in the click sequence the event belongs to. */
    uint8_t press_count;
    /** User data of the key that raised the event. */
    void *user_data;
} skey_message_t;

/**
 * @brief Single-producer single-consumer event queue.
 *
 * The storage comes from the application through skey_group_config_t and is
 * referenced rather than copied, so it must stay valid for the lifetime of the
 * group.
 */
typedef struct {
    /** Message storage; points to the buffer passed in the configuration. */
    skey_message_t *buffer;
    /**
     * Index mask derived from the configured queue size: buffer holds capacity
     * + 1 elements and at most capacity messages can be queued.
     */
    uint8_t capacity;
    /** Index of the next message to write; managed by skey_scan(). */
    volatile uint8_t write_index;
    /** Index of the next message to read; managed by skey_dispatch(). */
    volatile uint8_t read_index;
} skey_queue_t;

/**
 * @brief Key group.
 *
 * A group is not a collection of keys: it holds the callbacks, the
 * configuration and the event queue that a set of keys share, while the keys
 * themselves are passed separately to skey_scan(). Create it with
 * skey_init_group() rather than filling it in by hand.
 */
typedef struct {
    /** Read callback, called by skey_scan() for every key. */
    uint8_t (*read_cb)(void *user_data);
    /** Event callback, called with the events of one key. */
    void (*event_cb)(uint8_t events, uint8_t press_count, void *user_data);
    /** Callback execution mode, copied from the configuration. */
    skey_callback_mode_t callback_mode;
    /** How the gesture layer reacts to press level changes. */
    skey_debounce_mode_t press_debounce_mode;
    /** How the gesture layer reacts to release level changes. */
    skey_debounce_mode_t release_debounce_mode;
    /** Press debounce duration in ticks (at least 1). */
    uint16_t press_debounce_ticks;
    /** Release debounce duration in ticks (at least 1). */
    uint16_t release_debounce_ticks;
    /** Pressed duration after which a long press is reported. */
    uint16_t long_press_expired_ticks;
    /** Multi-press wait while pressed. */
    uint16_t multi_press_timeout_ticks;
    /** Multi-press wait while released. */
    uint16_t multi_release_timeout_ticks;
    /** Event queue holding the events not yet delivered by skey_dispatch(). */
    skey_queue_t queue;
} skey_group_t;

/**
 * @brief Group initialization configuration.
 *
 * Passed to skey_init_group(), which copies every field except the queue
 * storage, which is referenced. No defaulting is applied: a field left at 0
 * keeps the value 0, so assign every field you rely on.
 */
typedef struct {
    /**
     * Read callback, called by skey_scan(); it must return 0 when the key is
     * pressed and non-zero when it is released.
     */
    uint8_t (*read_cb)(void *user_data);
    /**
     * Event callback, called with the SKEY_EVENT_* flags, the press count and
     * the user data of the key that raised the event.
     */
    void (*event_cb)(uint8_t events, uint8_t press_count, void *user_data);
    /** Callback execution mode. */
    skey_callback_mode_t callback_mode;
    /**
     * Event queue storage; required in deferred mode and ignored in immediate
     * mode.
     */
    skey_message_t *queue_buffer;
    /**
     * Number of elements of queue_buffer: a power of two in
     * [SKEY_MIN_QUEUE_SIZE, SKEY_MAX_QUEUE_SIZE].
     */
    uint16_t queue_size;
    /** How the gesture layer reacts to press level changes. */
    skey_debounce_mode_t press_debounce_mode;
    /** How the gesture layer reacts to release level changes. */
    skey_debounce_mode_t release_debounce_mode;
    /** Press debounce duration in ticks (at least 1). */
    uint16_t press_debounce_ticks;
    /** Release debounce duration in ticks (at least 1). */
    uint16_t release_debounce_ticks;
    /** Pressed duration after which a long press is reported. */
    uint16_t long_press_expired_ticks;
    /**
     * Multi-press wait while pressed, after which
     * SKEY_EVENT_MULTI_PRESS_TIMEOUT is reported.
     */
    uint16_t multi_press_timeout_ticks;
    /**
     * Multi-press wait while released, after which
     * SKEY_EVENT_MULTI_RELEASE_TIMEOUT is reported; single, double and multi
     * clicks are typically resolved here through the press count.
     */
    uint16_t multi_release_timeout_ticks;
} skey_group_config_t;

/**
 * @brief Initialize a key and bind its user data.
 *
 * The whole key state is cleared, so calling this function on an uninitialized
 * object is safe.
 *
 * @param key Key object; must not be NULL.
 * @param user_data Value passed back to read_cb and event_cb for this key; may
 *                  be NULL.
 */
void skey_init_key(skey_t *key, void *user_data);
/**
 * @brief Initialize a group from a configuration.
 *
 * Copies the configuration into the group and empties its event queue.
 *
 * @param group Group object; must not be NULL, and its previous contents are
 *              discarded.
 * @param config Configuration to copy; must not be NULL and is only read.
 *
 * @note config->read_cb and config->event_cb must not be NULL.
 * @note config->queue_buffer must not be NULL in SKEY_CALLBACK_MODE_DEFERRED
 *       mode, and config->queue_size must then be a power of two between
 *       SKEY_MIN_QUEUE_SIZE and SKEY_MAX_QUEUE_SIZE.
 * @note The queue fields are ignored in SKEY_CALLBACK_MODE_IMMEDIATE mode.
 * @note config->queue_buffer is referenced, not copied, so it must stay valid
 *       for the lifetime of the group. Every other field is copied, so config
 *       itself may be a temporary that goes out of scope afterwards.
 */
void skey_init_group(skey_group_t *group, const skey_group_config_t *config);
/**
 * @brief Scan key states and generate key events.
 *
 * Reads every key through the group's read_cb, advances its debounce and
 * gesture state machines, and reports the resulting events, which are a bitwise
 * OR of the SKEY_EVENT_* flags.
 *
 * With SKEY_CALLBACK_MODE_IMMEDIATE the callback is executed as soon as an
 * event is generated; with SKEY_CALLBACK_MODE_DEFERRED the events are pushed
 * into the group's event queue for skey_dispatch().
 *
 * @param keys Array of key objects; must not be NULL and every element must
 *             have been initialized with skey_init_key().
 * @param key_count Number of keys in keys.
 * @param group Group the keys belong to; must not be NULL.
 * @return Number of events dropped because the event queue was full; always 0
 *         in immediate mode.
 *
 * @note Call this function periodically at a fixed period: press, release, long
 *       press and multi-press timeouts are all measured in ticks.
 * @note For a given group, only one execution context may call this function at
 *       a time: the key state update is protected by the lock abstraction,
 *       while enqueueing and the event callback run outside the critical
 *       section. Different groups are independent of each other.
 */
int skey_scan(skey_t keys[], uint8_t key_count, skey_group_t *group);
/**
 * @brief Process queued events and run the event callback.
 *
 * Drains at most max_event_count events from the group's event queue. Only
 * applicable to SKEY_CALLBACK_MODE_DEFERRED.
 *
 * @param max_event_count Maximum number of events processed by this call.
 * @param group Group the keys belong to; must not be NULL.
 *
 * @note For a given group, only one execution context may call this function at
 *       a time: it is the single-consumer side of the group's event queue,
 *       while skey_scan() is the producer.
 * @note Different groups are independent of each other and may be dispatched
 *       from different execution contexts.
 */
void skey_dispatch(uint8_t max_event_count, skey_group_t *group);

#ifdef __cplusplus
}
#endif

#endif
