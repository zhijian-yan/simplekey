<h1 align="center">simplekey</h1>

<p align="center">
<a href="README.md">English</a> | <a href="README_zh.md">简体中文</a>
</p>

<p align="center">
Lightweight Embedded Key Scanning Library
</p>

## Features

* Group-based key management: keys in the same group share callbacks, configuration, and an event queue
* Two-layer state machine: signal layer + gesture layer
* Independent press/release debounce modes (immediate / deferred)
* Debouncing, long-press, multi-press, and timeout detection
* Immediate and deferred callback execution modes
* SPSC event queue backed by a user-provided buffer
* No dynamic memory allocation
* Platform-independent lock abstraction

## Installation

### Git Submodule

```bash
git submodule add https://github.com/zhijian-yan/simplekey.git
```

### Direct Integration

Add the following files to your project:

* `simplekey.c`
* `simplekey.h`

## Quick Start

### 1. Create Keys, a Buffer and a Config

```c
#define KEY_NUM 2

skey_t keys[KEY_NUM];
skey_group_t group;
skey_group_config_t config;         /* initializes group, may be a local */
skey_message_t queue_buffer[16];    /* queue_size must be a power of two */
```

`skey_t` and `skey_group_t` are initialized by `skey_init_key()` and `skey_init_group()`, so they do not need to be zero-initialized beforehand. `config` is a plain value that is only read, so it may live on the stack.

### 2. Configure and Initialize the Group

```c
config.read_cb = skey_read_cb;
config.event_cb = skey_event_cb;
config.callback_mode = SKEY_CALLBACK_MODE_DEFERRED;

config.press_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER;
config.release_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER;

config.press_debounce_ticks = 1;
config.release_debounce_ticks = 1;
config.long_press_expired_ticks = 100;
config.multi_press_timeout_ticks = 30;
config.multi_release_timeout_ticks = 30;

config.queue_buffer = queue_buffer; /* required in deferred mode */
config.queue_size = 16;             /* elements in queue_buffer */

skey_init_key(&keys[0], (void *)KEY1_PIN);
skey_init_key(&keys[1], (void *)KEY2_PIN);
skey_init_group(&group, &config);
```

Every field of `config` is copied into `group`, so `config` does not have to stay alive after `skey_init_group()` returns. In `SKEY_CALLBACK_MODE_DEFERRED` mode `queue_buffer` and `queue_size` are required; in `SKEY_CALLBACK_MODE_IMMEDIATE` mode they are ignored and may be left at `0`.

### 3. Implement the Read Callback

```c
uint8_t skey_read_cb(void *user_data) {
    /* Return 0 when pressed, non-zero when released */
    if (gpio_get_level((int)user_data) == 0)
        return 0;
    return 1;
}
```

### 4. Implement the Event Callback

```c
void skey_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    if (events & SKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        printf("key[%d] pressed:%d\r\n", (int)user_data, press_count);
}
```

`events` is a bitmask; multiple events may be delivered in a single callback, so test them with bitwise AND.
### 5. Scan Keys Periodically

```c
void timer_callback(void) {
    /* Call at a fixed period, e.g. every 10 ms */
    skey_scan(keys, KEY_NUM, &group);
}
```

### 6. Dispatch Events

```c
while (1) {
    skey_dispatch(8, &group);
}
```

`skey_dispatch()` is only required in `SKEY_CALLBACK_MODE_DEFERRED` mode.

### 7. Complete Example

```c
#include "simplekey.h"
#include <stdio.h>

#define KEY_NUM 2
#define KEY1_PIN 1
#define KEY2_PIN 2
#define PRESSED_LEVEL 0

skey_t keys[KEY_NUM];
skey_group_t group;
skey_group_config_t config;
skey_message_t queue_buffer[16];

uint8_t skey_read_cb(void *user_data) {
    if (gpio_get_level((int)user_data) == PRESSED_LEVEL)
        return 0;
    return 1;
}

void skey_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    if (events & SKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);
    else if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        printf("key[%d] pressed:%d\r\n", (int)user_data, press_count);
}

void timer_callback(void) {
    skey_scan(keys, KEY_NUM, &group);
}

int main(void) {
    hardware_init();

    config.read_cb = skey_read_cb;
    config.event_cb = skey_event_cb;
    config.callback_mode = SKEY_CALLBACK_MODE_DEFERRED;
    config.press_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER;
    config.release_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER;
    config.press_debounce_ticks = 1;
    config.release_debounce_ticks = 1;
    config.long_press_expired_ticks = 100;
    config.multi_press_timeout_ticks = 30;
    config.multi_release_timeout_ticks = 30;
    config.queue_buffer = queue_buffer;
    config.queue_size = 16;

    skey_init_key(&keys[0], (void *)KEY1_PIN);
    skey_init_key(&keys[1], (void *)KEY2_PIN);
    skey_init_group(&group, &config);

    while (1) {
        skey_dispatch(8, &group);
    }
    return 0;
}
```

## Design

simplekey uses a polling-based key scanning mechanism.

The application periodically calls:

```c
skey_scan(keys, key_count, &group);
```

For example, every 10 ms.

### Group Model

Keys are managed in units of "groups":

* `skey_t` holds the runtime state of a single key
* `skey_group_t` holds the read callback, event callback, debounce modes, timing thresholds, and event queue shared by a group of keys

Each `skey_scan()` call processes every key in the passed array as follows:

1. Read the level through `group->read_cb()`
2. Run the signal-layer state machine for sampling and debouncing
3. Run the gesture-layer state machine for long-press, multi-press, and timeout detection
4. Emit events to either the callback or the event queue

```text
             skey_scan()
                  │
        ┌─────────┴─────────┐
        │  process each key  │
        ▼                   ▼
   Signal FSM           Gesture FSM
 (sample/debounce)  (long/multi/timeout)
        │                   │
        └─────────┬─────────┘
                  ▼
              event bitmask
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
  immediate callback    event queue
   (IMMEDIATE)              │
                           ▼
                   skey_dispatch()
                           │
                           ▼
                    deferred callback
```

### State Encoding

The entire state of a key is packed into a single `state` byte:

| Bits | Field           | Description                                                        |
| ---- | --------------- | ------------------------------------------------------------------ |
| 0-2  | signal state    | `IDLE` / `PRESS_DEBOUNCE` / `PRESSED` / `RELEASE_DEBOUNCE` / `RELEASED` |
| 3-4  | gesture state   | `IDLE` / `PRESSED` / `RELEASED`                                    |
| 5    | `LONG_PRESSED`  | long press has been triggered                                      |
| 6    | `MULTI_PRESSED` | a multi-press sequence is in progress                              |

### Signal-Layer State Machine

The signal layer samples the key and performs debouncing. The input is the level returned by the user read callback (`0` = pressed, non-zero = released).

```text
          level == 0
   IDLE ─────────────▶ PRESS_DEBOUNCE
    ▲                        │ debounce confirmed
    │                        ▼
    │                     PRESSED
    │                        │ level != 0
    │                        ▼
    └──── RELEASED ◀──── RELEASE_DEBOUNCE
            debounce confirmed
```

| Current State      | Condition                                                   | Action                                                          |
| ------------------ | ----------------------------------------------------------- | --------------------------------------------------------------- |
| `IDLE`             | `level == 0`                                                | `ticks = 0`, → `PRESS_DEBOUNCE`, emit `PRESS_EAGER`             |
| `PRESS_DEBOUNCE`   | `ticks >= press_debounce_ticks` and `level == 0`            | `ticks = 0`, → `PRESSED`, emit `PRESS_DEFER`                    |
| `PRESS_DEBOUNCE`   | `ticks >= press_debounce_ticks` and `level != 0`            | `key->state = 0`, invalidate the press (bounce)                 |
| `PRESS_DEBOUNCE`   | otherwise                                                   | `ticks++`                                                       |
| `PRESSED`          | `level != 0`                                                | `ticks = 0`, → `RELEASE_DEBOUNCE`, emit `RELEASE_EAGER`         |
| `PRESSED`          | otherwise                                                   | `ticks++` (capped at `SKEY_MAX_TICK`)                           |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` and `level != 0`          | `ticks = 0`, → `RELEASED`, emit `RELEASE_DEFER`                 |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` and `level == 0`          | `key->state = 0`, invalidate the press (bounce)                 |
| `RELEASE_DEBOUNCE` | otherwise                                                   | `ticks++`                                                       |
| `RELEASED`         | `level == 0`                                                | `ticks = 0`, gesture → `IDLE`, → `PRESS_DEBOUNCE`, emit `PRESS_EAGER` |
| `RELEASED`         | otherwise                                                   | `ticks++`                                                       |

#### Transition Invalidation (Bounce)

A bounce is a level that returns to its previous value before the debounce has confirmed the transition. A bounce that survives the debounce is treated as contact noise, so the press it belongs to is invalidated: the whole `state` byte is cleared, as if the press had never happened.

Clearing the byte resets the signal-layer state, the gesture-layer state, and the gesture-layer flags in a single operation. The gesture layer is not notified separately, and it does not need to be: its own state lives in the same byte and is cleared with it, so the two layers stay consistent by construction.

The trade-off is deliberate: the gesture layer cannot tell a bounce apart from an interrupted press, so a sequence that was already under way is discarded together with the bounce. Two consequences follow, and both are visible only when the corresponding debounce is set above `1` tick:

* **Release bounce.** A long press that was already reported (`LONG_PRESS`) never gets its matching `LONG_RELEASE`. The press sequence is dropped instead of being finished.
* **Release bounce.** The click sequence is dropped as well, so `MULTI_RELEASE_TIMEOUT` — the event [Timing](#timing) recommends for single/double-click detection — is not emitted for that sequence. With `release_debounce_ticks = 2`, a double-click whose second release bounces still reaches `press_count == 2`, but the timeout event never arrives, so the click is not reported.
* **Press bounce.** This one is harmless: the gesture layer has not committed to the press yet, so a later press is tracked normally and still counts as a single click.

The trade-off buys bounce resistance at the cost of losing the whole sequence rather than just the bounce. If you rely on `LONG_RELEASE` or on `MULTI_RELEASE_TIMEOUT` for click counting, keep `release_debounce_ticks` at `1`, or treat an invalidated sequence as lost input in the application. A press bounce never invalidates a sequence that is already being tracked, so `press_debounce_ticks` can be raised freely.

### Gesture-Layer State Machine

The gesture layer consumes signal-layer events, applies the configured debounce modes, and emits long-press, multi-press, and timeout events.

```text
   IDLE ──(press event)──▶ PRESSED ──(release event)──▶ RELEASED
    ▲                                                     │
    │            sequence finished (reset)                │
    └─────────────────────────────────────────────────────┘
```

| Current State | Condition                                                         | Action                                                                                         |
| ------------- | ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| `IDLE`        | press event (`PRESS_DEFER` / `PRESS_EAGER` per `press_debounce_mode`)   | → `PRESSED`; if `MULTI_PRESSED` is set then `press_count++`, otherwise `press_count = 1` and set `MULTI_PRESSED` |
| `PRESSED`     | release event (`RELEASE_DEFER` / `RELEASE_EAGER` per `release_debounce_mode`) | → `RELEASED`                                                                             |
| `PRESSED`     | `LONG_PRESSED` not set and `ticks > long_press_expired_ticks`     | set `LONG_PRESSED`, emit `LONG_PRESS`                                                          |
| `PRESSED`     | `MULTI_PRESSED` set and `ticks > multi_press_timeout_ticks`       | clear `MULTI_PRESSED`, emit `MULTI_PRESS_TIMEOUT`                                              |
| `RELEASED`    | `LONG_PRESSED` set                                                | clear `LONG_PRESSED`, emit `LONG_RELEASE`                                                      |
| `RELEASED`    | `MULTI_PRESSED` set and `ticks > multi_release_timeout_ticks`     | clear `MULTI_PRESSED`, emit `MULTI_RELEASE_TIMEOUT`                                            |
| `RELEASED`    | `MULTI_PRESSED` not set and signal layer is `RELEASED`            | reset the whole `state` (click sequence finished)                                             |

### Debounce Modes

The signal layer emits events both on level transitions and on debounce confirmation. Which of these the gesture layer reacts to is controlled by the debounce mode.

| Mode                       | Meaning                                                            |
| -------------------------- | ----------------------------------------------------------------- |
| `SKEY_DEBOUNCE_MODE_DEFER` | React after debounce confirmation (more bounce-resistant)          |
| `SKEY_DEBOUNCE_MODE_EAGER` | React immediately on level transition (lower latency, less filtering) |

Press and release debouncing are configured independently through `config.press_debounce_mode` and `config.release_debounce_mode`, which `skey_init_group()` copies into the group.

### Events

| Event                              | Description                                                    |
| ---------------------------------- | ------------------------------------------------------------- |
| `SKEY_EVENT_PRESS_EAGER`           | level changed to "pressed" (before debounce confirmation)      |
| `SKEY_EVENT_PRESS_DEFER`           | press debounce confirmed                                       |
| `SKEY_EVENT_RELEASE_EAGER`         | level changed to "released" (before debounce confirmation)     |
| `SKEY_EVENT_RELEASE_DEFER`         | release debounce confirmed                                     |
| `SKEY_EVENT_LONG_PRESS`            | pressed duration exceeded `long_press_expired_ticks`           |
| `SKEY_EVENT_LONG_RELEASE`          | released after a long press                                    |
| `SKEY_EVENT_MULTI_PRESS_TIMEOUT`   | multi-press wait while pressed exceeded `multi_press_timeout_ticks` |
| `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` | multi-press wait while released exceeded `multi_release_timeout_ticks` |

Multiple events may be produced in the same callback; `events` is the bitwise OR of them.

### Timing

`key->ticks` accumulates while pressed (`PRESSED`) and while released (`RELEASED`), and drives long-press and timeout decisions.

```text
ticks
  ├── accumulates while pressed
  │     ├── long_press_expired_ticks     → long press
  │     └── multi_press_timeout_ticks    → multi-press timeout while pressed
  │
  └── accumulates while released
        └── multi_release_timeout_ticks  → multi-press timeout while released
```

Users typically determine single-click, double-click, or multi-click actions in the `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` event using `press_count`:

```c
if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT) {
    switch (press_count) {
    case 1:
        printf("single click\n");
        break;
    case 2:
        printf("double click\n");
        break;
    case 3:
        printf("triple click\n");
        break;
    }
}
```

### Callback Execution Model

simplekey supports two callback modes.

#### Immediate Mode

```c
SKEY_CALLBACK_MODE_IMMEDIATE
```

The callback is executed as soon as the event is generated.

```text
Scan
  ↓
Generate Event
  ↓
Execute Callback
```

Advantages:

* Lowest latency
* No event queue required

#### Deferred Mode

```c
SKEY_CALLBACK_MODE_DEFERRED
```

Events are first pushed into the group's event queue:

```text
Scan
  ↓
Generate Event
  ↓
Push Queue
```

They are later processed by `skey_dispatch()`:

```text
Dispatch
  ↓
Execute Callback
```

Advantages:

* Suitable for interrupt-driven scanning
* Callbacks run in task or main-loop context
* Prevents lengthy callbacks from affecting scan timing

### Event Queue

Events within a group are passed through a single-producer single-consumer (SPSC) ring buffer whose storage is provided by the user:

```c
typedef struct {
    skey_message_t *buffer;   /* user-provided buffer of queue_size elements */
    uint8_t capacity;         /* index mask = queue_size - 1, set by skey_init_group() */
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

`capacity` is the index mask maintained by `skey_init_group()`: `buffer` holds `capacity + 1` elements, and at most `capacity` messages can be queued. The ring always keeps one slot free to tell a full queue from an empty one, so a queue built from `queue_size = 16` holds 15 messages.

Requirements:

* `config.queue_size` is the element count of `queue_buffer`: a power of two between `SKEY_MIN_QUEUE_SIZE` (`2`) and `SKEY_MAX_QUEUE_SIZE` (`256`)
* In `SKEY_CALLBACK_MODE_IMMEDIATE` mode the queue is unused and these fields may be left at `0`
* When the queue is full, events are dropped and the return value of `skey_scan()` accumulates the number of drops

### Concurrency Model

simplekey uses an SPSC (Single Producer Single Consumer) model.

**Producer**

* `skey_scan()`

**Consumer**

* `skey_dispatch()`

The event queue is safe under the SPSC model, and `skey_scan()` uses the lock abstraction to protect the key state (including the signal-layer, gesture-layer, and flag bits) while it is being updated.

The critical section covers only the state update. Enqueuing into the event queue and invoking `event_cb` happen after `skey_unlock()`; they rely on the SPSC model above rather than on the lock.

simplekey abstracts the platform-specific lock through two interfaces:

```c
static inline int skey_lock(void)
{
    /* Disable interrupts if needed */
    return 0;
}

static inline void skey_unlock(int skey_lock_state)
{
    /* Restore interrupt state */
    (void)skey_lock_state;
}
```

The default implementation is a no-op; platforms that require interrupt safety can provide their own implementation.

The following APIs must follow the SPSC model: only one execution context may call each function at a time:

* `skey_scan()`
* `skey_dispatch()`

## API Reference

### skey_init_key

```c
void skey_init_key(skey_t *key, void *user_data);
```

Initialize one key and bind its user data.

**Parameters**

* `key` - Key object to initialize, must not be `NULL`
* `user_data` - Value passed back to `read_cb` and `event_cb` for this key, may be `NULL`

The whole key state is cleared, so calling this on an uninitialized object is safe.

---

### skey_init_group

```c
void skey_init_group(skey_group_t *group, const skey_group_config_t *config);
```

Initialize a group from a configuration.

**Parameters**

* `group` - Group object to initialize, must not be `NULL`; its previous contents are discarded
* `config` - Configuration to copy, must not be `NULL` and is not modified

**Notes**

* `config->read_cb` and `config->event_cb` must not be `NULL`
* In `SKEY_CALLBACK_MODE_DEFERRED` mode, `config->queue_buffer` must not be `NULL` and `config->queue_size` must be a power of two between `SKEY_MIN_QUEUE_SIZE` (`2`) and `SKEY_MAX_QUEUE_SIZE` (`256`)
* In `SKEY_CALLBACK_MODE_IMMEDIATE` mode the queue fields are ignored
* Every field is copied, so `config` may be a local that goes out of scope afterwards

---

### skey_scan

```c
int skey_scan(skey_t keys[], uint8_t key_count, skey_group_t *group);
```

Scan key states.

* With `SKEY_CALLBACK_MODE_IMMEDIATE`, the callback is executed as soon as an event is generated
* With `SKEY_CALLBACK_MODE_DEFERRED`, events are pushed into the group's event queue

**Parameters**

* `keys` - Key object array, must not be `NULL` and must have been initialized with `skey_init_key()`
* `key_count` - Number of keys
* `group` - Group the keys belong to, must not be `NULL`

**Return Value**

* `int` - Number of events that could not be enqueued because the queue was full (always `0` in immediate mode); check it if you need to notice dropped events

---

### skey_dispatch

```c
void skey_dispatch(uint8_t max_event_count, skey_group_t *group);
```

Process the group's event queue and execute callbacks.

Only valid in `SKEY_CALLBACK_MODE_DEFERRED` mode.

**Parameters**

* `max_event_count` - Maximum events processed per call
* `group` - Group the keys belong to

---

### skey_lock / skey_unlock

```c
static inline int skey_lock(void);
static inline void skey_unlock(int skey_lock_state);
```

Platform-specific lock abstraction.

The lock state returned by `skey_lock()` is passed back to `skey_unlock()` to restore the critical section.

The default implementation is a no-op; platforms that require interrupt safety can provide their own implementation.

## Data Structures

### skey_t

```c
typedef struct {
    volatile uint16_t ticks;
    volatile uint8_t press_count;
    volatile uint8_t state;
    void *user_data;
} skey_t;
```

* `ticks` - timer used for debouncing, long-press, and timeout decisions
* `press_count` - number of presses within the current click sequence
* `state` - packed signal state, gesture state, and flags
* `user_data` - user data passed to the read/event callbacks

Create it with `skey_init_key()`, which clears the whole object and binds the user data. The internal fields are managed by the library and must not be written directly.

### skey_message_t

```c
typedef struct {
    uint8_t events;
    uint8_t press_count;
    void *user_data;
} skey_message_t;
```

A single message in the event queue.

### skey_queue_t

```c
typedef struct {
    skey_message_t *buffer;
    uint8_t capacity;
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

SPSC event queue.

### skey_group_config_t

```c
typedef struct {
    uint8_t (*read_cb)(void *user_data);
    void (*event_cb)(uint8_t events, uint8_t press_count, void *user_data);
    skey_callback_mode_t callback_mode;
    skey_message_t *queue_buffer;
    uint16_t queue_size;
    skey_debounce_mode_t press_debounce_mode;
    skey_debounce_mode_t release_debounce_mode;
    uint16_t press_debounce_ticks;
    uint16_t release_debounce_ticks;
    uint16_t long_press_expired_ticks;
    uint16_t multi_press_timeout_ticks;
    uint16_t multi_release_timeout_ticks;
} skey_group_config_t;
```

Configuration consumed by `skey_init_group()`. It is read once and copied into the group, so it may be a local value and does not have to be initialized to anything beforehand as long as every field used below is assigned.

* `read_cb` / `event_cb` - callbacks, both required
* `callback_mode` - `SKEY_CALLBACK_MODE_DEFERRED` or `SKEY_CALLBACK_MODE_IMMEDIATE`
* `queue_buffer` / `queue_size` - queue storage and its element count (a power of two between `SKEY_MIN_QUEUE_SIZE` and `SKEY_MAX_QUEUE_SIZE`); required in deferred mode, ignored in immediate mode
* `press_debounce_mode` / `release_debounce_mode` - how the gesture layer reacts to the signal layer: `SKEY_DEBOUNCE_MODE_DEFER` waits for the debounce to confirm, `SKEY_DEBOUNCE_MODE_EAGER` reacts on the level change
* `press_debounce_ticks` / `release_debounce_ticks` - debounce durations, at least `1`
* `long_press_expired_ticks` - long-press threshold
* `multi_press_timeout_ticks` - multi-press timeout while pressed
* `multi_release_timeout_ticks` - multi-press timeout while released

No defaulting is applied: a field left at `0` keeps the value `0`, so assign every field you rely on.

### skey_group_t

```c
typedef struct {
    uint8_t (*read_cb)(void *user_data);
    void (*event_cb)(uint8_t events, uint8_t press_count, void *user_data);
    skey_callback_mode_t callback_mode;
    skey_debounce_mode_t press_debounce_mode;
    skey_debounce_mode_t release_debounce_mode;
    uint16_t press_debounce_ticks;
    uint16_t release_debounce_ticks;
    uint16_t long_press_expired_ticks;
    uint16_t multi_press_timeout_ticks;
    uint16_t multi_release_timeout_ticks;
    skey_queue_t queue;
} skey_group_t;
```

Key group configuration.

A group is not a collection of keys: it holds the callbacks, the configuration, and the event queue that a set of keys share. The keys themselves are passed separately to `skey_scan()` as a `skey_t` array, so `group` here means "the configuration shared by these keys", not "the keys in this group".

* `read_cb` - read callback, returns `0` when pressed and non-zero when released
* `event_cb` - event callback
* `callback_mode` - callback execution mode
* `press_debounce_mode` / `release_debounce_mode` - press/release debounce modes
* `press_debounce_ticks` / `release_debounce_ticks` - press/release debounce durations
* `long_press_expired_ticks` - long-press threshold
* `multi_press_timeout_ticks` - multi-press timeout while pressed
* `multi_release_timeout_ticks` - multi-press timeout while released
* `queue` - event queue

Create it with `skey_init_group()` rather than filling it in by hand.

## Macros and Enums

### Event Macros

All are bitmasks:

| Macro                              | Value      |
| ---------------------------------- | ---------- |
| `SKEY_EVENT_PRESS_DEFER`           | `1U << 0`  |
| `SKEY_EVENT_PRESS_EAGER`           | `1U << 1`  |
| `SKEY_EVENT_RELEASE_DEFER`         | `1U << 2`  |
| `SKEY_EVENT_RELEASE_EAGER`         | `1U << 3`  |
| `SKEY_EVENT_LONG_PRESS`            | `1U << 4`  |
| `SKEY_EVENT_LONG_RELEASE`          | `1U << 5`  |
| `SKEY_EVENT_MULTI_PRESS_TIMEOUT`   | `1U << 6`  |
| `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` | `1U << 7`  |

Helper macros:

```c
#define skey_event_set(events, value)  (events |= value)
#define skey_event_get(events, value)  ((events) & value)
```

### skey_callback_mode_t

```c
typedef enum {
    SKEY_CALLBACK_MODE_DEFERRED = 0,
    SKEY_CALLBACK_MODE_IMMEDIATE,
} skey_callback_mode_t;
```

Callback execution mode.

### skey_debounce_mode_t

```c
typedef enum {
    SKEY_DEBOUNCE_MODE_DEFER = 0,
    SKEY_DEBOUNCE_MODE_EAGER,
} skey_debounce_mode_t;
```

Debounce mode.

### SKEY_MAX_TICK

Maximum tick counter value, `0xFFFF` (`65535`).

`ticks` stops increasing once this value is reached.

### SKEY_MAX_COUNT

Maximum press count, `0xFF` (`255`).

`press_count` stops increasing once this value is reached.

### SKEY_MAX_QUEUE_SIZE

Maximum number of elements of a queue buffer (`256`).

Because the ring buffer always leaves one slot free, such a queue holds at most 255 messages.

### SKEY_MIN_QUEUE_SIZE

Minimum number of elements of a queue buffer (`2`).

A queue of `2` elements holds at most one message.
