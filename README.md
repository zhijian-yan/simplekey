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

### 1. Create Keys and a Group

```c
#define KEY_NUM 2

skey_t keys[KEY_NUM];
skey_group_t group;
skey_message_t queue_buffer[16];
```

Both `skey_t` and `skey_group_t` must be zero-initialized (automatic when declared as global or static objects).

### 2. Configure the Group

```c
group.read_cb = skey_read_cb;
group.event_cb = skey_event_cb;
group.cb_mode = SKEY_CALLBACK_MODE_DEFERRED;

group.press_db_mode = SKEY_DEBOUNCE_MODE_DEFER;
group.release_db_mode = SKEY_DEBOUNCE_MODE_DEFER;

group.press_debounce_ticks = 1;
group.release_debounce_ticks = 1;
group.long_press_expired_ticks = 100;
group.multi_press_timeout_ticks = 30;
group.multi_release_timeout_ticks = 30;

group.queue.buffer = queue_buffer;
group.queue.length = 16;
```

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
void skey_event_cb(uint8_t event, uint8_t press_count, void *user_data) {
    if (event & SKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);

    if (event & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        printf("key[%d] pressed:%d\r\n", (int)user_data, press_count);
}
```

`event` is a bitmask; multiple events may be delivered in a single callback, so test them with bitwise AND.

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
skey_message_t queue_buffer[16];

uint8_t skey_read_cb(void *user_data) {
    if (gpio_get_level((int)user_data) == PRESSED_LEVEL)
        return 0;
    return 1;
}

void skey_event_cb(uint8_t event, uint8_t press_count, void *user_data) {
    if (event & SKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);
    else if (event & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        printf("key[%d] pressed:%d\r\n", (int)user_data, press_count);
}

void timer_callback(void) {
    skey_scan(keys, KEY_NUM, &group);
}

int main(void) {
    hardware_init();

    keys[0].user_data = (void *)KEY1_PIN;
    keys[1].user_data = (void *)KEY2_PIN;

    group.read_cb = skey_read_cb;
    group.event_cb = skey_event_cb;
    group.cb_mode = SKEY_CALLBACK_MODE_DEFERRED;
    group.press_db_mode = SKEY_DEBOUNCE_MODE_DEFER;
    group.release_db_mode = SKEY_DEBOUNCE_MODE_DEFER;
    group.press_debounce_ticks = 1;
    group.release_debounce_ticks = 1;
    group.long_press_expired_ticks = 100;
    group.multi_press_timeout_ticks = 30;
    group.multi_release_timeout_ticks = 30;
    group.queue.buffer = queue_buffer;
    group.queue.length = 16;

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
skey_scan(keys, key_num, &group);
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
| `PRESS_DEBOUNCE`   | `ticks >= press_debounce_ticks` and `level != 0`            | reset the whole `state` (treated as bounce)                     |
| `PRESS_DEBOUNCE`   | otherwise                                                   | `ticks++`                                                       |
| `PRESSED`          | `level != 0`                                                | `ticks = 0`, → `RELEASE_DEBOUNCE`, emit `RELEASE_EAGER`         |
| `PRESSED`          | otherwise                                                   | `ticks++` (capped at `SKEY_MAX_TICK`)                           |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` and `level != 0`          | `ticks = 0`, → `RELEASED`, emit `RELEASE_DEFER`                 |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` and `level == 0`          | reset the whole `state` (treated as bounce)                     |
| `RELEASE_DEBOUNCE` | otherwise                                                   | `ticks++`                                                       |
| `RELEASED`         | `level == 0`                                                | `ticks = 0`, gesture → `IDLE`, → `PRESS_DEBOUNCE`, emit `PRESS_EAGER` |
| `RELEASED`         | otherwise                                                   | `ticks++`                                                       |

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
| `IDLE`        | press event (`PRESS_DEFER` / `PRESS_EAGER` per `press_db_mode`)   | → `PRESSED`; if `MULTI_PRESSED` is set then `press_count++`, otherwise `press_count = 1` and set `MULTI_PRESSED` |
| `PRESSED`     | release event (`RELEASE_DEFER` / `RELEASE_EAGER` per `release_db_mode`) | → `RELEASED`                                                                             |
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

Press and release debouncing are configured independently through `group.press_db_mode` and `group.release_db_mode`.

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

Multiple events may be produced in the same callback; `event` is the bitwise OR of them.

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
if (event & SKEY_EVENT_MULTI_RELEASE_TIMEOUT) {
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
    skey_message_t *buffer;   /* user-provided buffer */
    uint8_t length;           /* must be a power of two (max 128 for uint8_t) */
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

Requirements:

* When `buffer` is not `NULL`, `length` must be a power of two
* When `buffer` is `NULL`, deferred events are dropped (immediate mode is unaffected)
* When the queue is full, events are dropped and the return value of `skey_scan()` accumulates the number of drops

### Concurrency Model

simplekey uses an SPSC (Single Producer Single Consumer) model.

**Producer**

* `skey_scan()`

**Consumer**

* `skey_dispatch()`

The event queue is safe under the SPSC model, and `skey_scan()` uses the lock abstraction to protect the critical section while updating key state and generating events.

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

### skey_scan

```c
uint8_t skey_scan(skey_t keys[], uint8_t key_num, skey_group_t *group);
```

Scan key states.

* With `SKEY_CALLBACK_MODE_IMMEDIATE`, the callback is executed as soon as an event is generated
* With `SKEY_CALLBACK_MODE_DEFERRED`, events are pushed into the group's event queue

**Parameters**

* `keys` - Key object array
* `key_num` - Number of keys
* `group` - Group the keys belong to

**Return Value**

* Number of events that could not be enqueued because the queue was full (always `0` in immediate mode)

**Notes**

* `keys`, `group`, `group->read_cb`, and `group->event_cb` must not be `NULL`
* When `group->queue.buffer` is not `NULL`, `group->queue.length` must be a power of two

---

### skey_dispatch

```c
void skey_dispatch(uint8_t max_event_num, skey_group_t *group);
```

Process the group's event queue and execute callbacks.

Only valid in `SKEY_CALLBACK_MODE_DEFERRED` mode.

**Parameters**

* `max_event_num` - Maximum events processed per call
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

Must be zero-initialized before use.

### skey_message_t

```c
typedef struct {
    uint8_t event;
    uint8_t press_count;
    void *user_data;
} skey_message_t;
```

A single message in the event queue.

### skey_queue_t

```c
typedef struct {
    skey_message_t *buffer;
    uint8_t length;
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

SPSC event queue.

### skey_group_t

```c
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
} skey_group_t;
```

Key group configuration.

* `read_cb` - read callback, returns `0` when pressed and non-zero when released
* `event_cb` - event callback
* `cb_mode` - callback execution mode
* `press_db_mode` / `release_db_mode` - press/release debounce modes
* `press_debounce_ticks` / `release_debounce_ticks` - press/release debounce durations
* `long_press_expired_ticks` - long-press threshold
* `multi_press_timeout_ticks` - multi-press timeout while pressed
* `multi_release_timeout_ticks` - multi-press timeout while released
* `queue` - event queue

Must be zero-initialized before use.

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
#define skey_event_set(event, value)  (event |= value)
#define skey_event_get(event, value)  ((event) & value)
```

### skey_cb_mode_t

```c
typedef enum {
    SKEY_CALLBACK_MODE_DEFERRED = 0,
    SKEY_CALLBACK_MODE_IMMEDIATE,
} skey_cb_mode_t;
```

Callback execution mode.

### skey_db_mode_t

```c
typedef enum {
    SKEY_DEBOUNCE_MODE_DEFER = 0,
    SKEY_DEBOUNCE_MODE_EAGER,
} skey_db_mode_t;
```

Debounce mode.

### SKEY_MAX_TICK

Maximum tick counter value, `0xFFFF` (`65535`).

`ticks` stops increasing once this value is reached.

### SKEY_MAX_COUNT

Maximum press count, `0xFF` (`255`).

`press_count` stops increasing once this value is reached.
