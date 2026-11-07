<h1 align="center">SimpleKey</h1>

<p align="center"><a href="README.md">English</a> | <a href="README_zh.md">简体中文</a></p>

<p align="center"><em>Lightweight key scanning for embedded systems</em></p>

<p align="center">
<a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square"></a>
<img alt="Language" src="https://img.shields.io/badge/language-C99-blue.svg?style=flat-square">
<img alt="Dependencies" src="https://img.shields.io/badge/dependencies-none-brightgreen.svg?style=flat-square">
<img alt="Dynamic memory" src="https://img.shields.io/badge/dynamic_memory-none-brightgreen.svg?style=flat-square">
<img alt="Platform" src="https://img.shields.io/badge/platform-bare--metal_%7C_RTOS-lightgrey.svg?style=flat-square">
</p>

## Features

* Polling scan driven by the caller at a fixed period
* Group-based management: the keys of a group share the callbacks, the timing configuration and the event queue
* Two-layer state machine: a signal layer for sampling and debounce, a gesture layer for long press, multi press and timeouts
* Independent press and release debounce modes
* Debounce, long press, long release, multi press and timeout events
* A click count delivered with every event
* Immediate and deferred callbacks
* Event queue buffer provided by the application; dropped events are counted and reported
* No dynamic memory allocation, no OS dependency, C99
* The platform-specific queue ordering barriers are isolated behind porting hooks

## Porting

Porting SimpleKey means implementing the two hooks in `simplekey_port.h`; that file belongs to the application, so keep your own version across updates.

### Hooks

| Hook | Role |
| ---- | ---- |
| `SKEY_ACQUIRE()` | Ordering barrier after a queue index load, before the event it publishes is accessed |
| `SKEY_RELEASE()` | Ordering barrier before the queue index that publishes an event is stored |

### Default implementation

The shipped hooks are no-ops and are sufficient on a single-core target: the event queue is single-producer single-consumer, so its index and event writes need no explicit ordering.

### Example: multi-core

The queue is single-producer single-consumer, so it only needs real ordering barriers: `volatile` alone gives no ordering, so a core could see the index before the event.

```c
/* simplekey_port.h */
#include "cmsis_compiler.h"

#define SKEY_ACQUIRE() __DMB()
#define SKEY_RELEASE() __DMB()
```

With non-coherent caches, barriers are not enough: use non-cacheable memory or explicit cache maintenance.

## Usage

### 1. Define the objects and the callbacks

```c
#include "simplekey.h"

#define KEY_NUM  2
#define KEY1_PIN 1
#define KEY2_PIN 2

static skey_t keys[KEY_NUM];
static skey_group_t group;
static skey_message_t queue_buffer[16]; /* required in deferred mode */

/* Set by the event callback, handled by the application */
static volatile uint8_t click_count;

extern int gpio_get_level(int pin); /* the GPIO read of the application */

/* Return non-zero when the key is pressed; user_data is the pin number */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 1 : 0;
}

/* 1 = single click, 2 = double click, ... */
static void key_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    (void)user_data;

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        click_count = press_count;
}
```

`key_read_cb()` returns the logical press state, not the raw pin level: non-zero means pressed and 0 means released. The example therefore inverts an active-low button; an active-high button can return the pin level directly.

`events` is a bitwise OR of the `SKEY_EVENT_*` flags, so several events may arrive in one call and each has to be tested with `&`. The click count is resolved on `SKEY_EVENT_MULTI_RELEASE_TIMEOUT`, which is reported once the key has been released for longer than `multi_release_timeout_ticks`.

### 2. Initialize the keys and the group

```c
skey_init_key(&keys[0], (void *)(intptr_t)KEY1_PIN);
skey_init_key(&keys[1], (void *)(intptr_t)KEY2_PIN);

const skey_group_config_t config = {
    .read_cb = key_read_cb,
    .event_cb = key_event_cb,
    .callback_mode = SKEY_CALLBACK_MODE_DEFERRED,
    .queue_buffer = queue_buffer,
    .queue_size = 16,
    .press_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER,
    .release_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER,
    .press_debounce_ticks = 1,
    .release_debounce_ticks = 1,
    .long_press_expired_ticks = 100,
    .multi_press_timeout_ticks = 30,
    .multi_release_timeout_ticks = 30,
};
skey_init_group(&group, &config);
```

* `skey_init_key()` clears the whole key, so `user_data` is passed there and must not be assigned by hand
* No defaulting is applied: a field left at 0 keeps the value 0, so assign every field the application relies on
* `queue_size` must be a power of two in `[SKEY_MIN_QUEUE_SIZE, SKEY_MAX_QUEUE_SIZE]` and `queue_buffer` must have that many elements. One slot of the ring stays free, so 16 elements hold at most 15 pending events
* The queue fields are ignored in `SKEY_CALLBACK_MODE_IMMEDIATE`, where the callback runs inside `skey_scan()` and no event can be lost
* `queue_buffer` is referenced rather than copied, so it must stay valid for the lifetime of the group

### 3. Scan periodically and dispatch

```c
/* Called by the periodic timer interrupt, for example every 10 ms */
void key_scan_tick(void) {
    (void)skey_scan(keys, KEY_NUM, &group); /* non-zero: events were dropped */
}

for (;;) {
    skey_dispatch(8, &group); /* deferred mode only */
    /* handle click_count here */
}
```

The return value of `skey_scan()` is the number of events dropped because the event queue was full; it is always 0 in immediate mode. Within a group, `skey_scan()` and `skey_dispatch()` each accept a single execution context; different groups are independent. Here the scan runs in interrupt context and the dispatch in the main loop; the event queue is single-producer single-consumer, so the shipped hooks work as they are.

### 4. Pick the timing values

All durations are counted in ticks of the scan period. With a 10 ms scan period:

| Field | Example | Meaning |
| ----- | ------- | ------- |
| `press_debounce_ticks` | 1 | The pressed state is confirmed about 10 ms after the press is first sampled |
| `release_debounce_ticks` | 1 | The released state is confirmed about 10 ms after the release is first sampled |
| `long_press_expired_ticks` | 100 | Long press reported 100 ticks (about 1 s) after the press is confirmed |
| `multi_press_timeout_ticks` | 30 | Multi-press timeout reported 30 ticks (about 300 ms) after the press is confirmed |
| `multi_release_timeout_ticks` | 30 | Click sequence resolved 30 ticks (about 300 ms) after the release is confirmed |

Every threshold is counted from the moment the debounce is confirmed: the tick counter restarts at 1 in the scan that confirms it, so a threshold of N is reached after exactly N ticks. `long_press_expired_ticks = 100` therefore means 100 ticks after the press is confirmed. Measured from the state change itself, the total time is longer by the debounce, that is by `press_debounce_ticks` or `release_debounce_ticks` ticks.

### 5. Complete example

```c
#include "simplekey.h"

#define KEY_NUM  2
#define KEY1_PIN 1
#define KEY2_PIN 2

static skey_t keys[KEY_NUM];
static skey_group_t group;
static skey_message_t queue_buffer[16];

/* Set by the event callback, handled by the application */
static volatile uint8_t click_count;

extern int gpio_get_level(int pin); /* the GPIO read of the application */

/* Return non-zero when the key is pressed; user_data is the pin number */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 1 : 0;
}

/* 1 = single click, 2 = double click, ... */
static void key_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    (void)user_data;

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        click_count = press_count;
}

/* Called by the periodic timer interrupt, for example every 10 ms */
void key_scan_tick(void) {
    (void)skey_scan(keys, KEY_NUM, &group);
}

int main(void) {
    skey_init_key(&keys[0], (void *)(intptr_t)KEY1_PIN);
    skey_init_key(&keys[1], (void *)(intptr_t)KEY2_PIN);

    const skey_group_config_t config = {
        .read_cb = key_read_cb,
        .event_cb = key_event_cb,
        .callback_mode = SKEY_CALLBACK_MODE_DEFERRED,
        .queue_buffer = queue_buffer,
        .queue_size = 16,
        .press_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER,
        .release_debounce_mode = SKEY_DEBOUNCE_MODE_DEFER,
        .press_debounce_ticks = 1,
        .release_debounce_ticks = 1,
        .long_press_expired_ticks = 100,
        .multi_press_timeout_ticks = 30,
        .multi_release_timeout_ticks = 30,
    };
    skey_init_group(&group, &config);

    for (;;) {
        skey_dispatch(8, &group);
        /* handle click_count here */
    }
}
```
