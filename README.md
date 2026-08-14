<h1 align="center">tickey</h1>

<p align="center">
<a href="README.md">English</a> | <a href="README_zh.md">简体中文</a>
</p>

<p align="center">
Lightweight Embedded Key Scanning Library
</p>

## Features

* Key debouncing
* Short press, long press, and multi-press detection
* Immediate and deferred callback execution modes
* SPSC (Single Producer Single Consumer) event queue
* No dynamic memory allocation
* Platform-independent lock abstraction

## Installation

### Git Submodule

```bash
git submodule add https://github.com/zhijian-yan/tickey.git
```

### Direct Integration

Add the following files to your project:

* `tickey.c`
* `tickey.h`

## Quick Start

### 1. Create a Key Object

```c
tkey_t key;
```

### 2. Initialize the Key

```c
tkey_init(&key, TKEY_CB_MODE_DEFERRED, tkey_event_callback,
          tkey_read_callback, NULL);
```

### 3. Implement the Read Callback

```c
int tkey_read_callback(void *user_data) {
    if (gpio_get_level((int)user_data) == PRESSED_LEVEL)
        return 1;
    else
        return 0;
}
```

### 4. Implement the Event Callback

```c
void tkey_event_callback(tkey_t *key, tkey_event_t event, uint8_t press_count,
                         void *user_data) {
    if (event & TKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);
    else if (event & TKEY_EVENT_RELEASE_TIMEOUT)
        printf("key[%d] pressed:[%d]\r\n", (int)user_data, press_count);
}
```

### 5. Scan Keys Periodically

```c
void timer_callback(void) {
    tkey_scan(&key, 1);
}
```

### 6. Dispatch Events

```c
while (1) {
    tkey_dispatch(8);
}
```

### Complete Example

```c
#include "tickey.h"
#include <stdio.h>

#define key1_pin 1
#define key2_pin 2
#define PRESSED_LEVEL 0

tkey_t key[2];

int tkey_read_callback(void *user_data) {
    if (gpio_get_level((int)user_data) == PRESSED_LEVEL)
        return 1;
    else
        return 0;
}

void tkey_event_callback(tkey_t *key, tkey_event_t event, uint8_t press_count,
                         void *user_data) {
    if (event & TKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);
    else if (event & TKEY_EVENT_RELEASE_TIMEOUT)
        printf("key[%d] pressed:[%d]\r\n", (int)user_data, press_count);
}

void timer_callback(void) {
    tkey_scan(key, sizeof(key) / sizeof(tkey_t));
}

int main(void) {
    hardware_init();
    tkey_init(&key[0], TKEY_CB_MODE_DEFERRED, tkey_event_callback,
              tkey_read_callback, (void *)key1_pin);
    tkey_init(&key[1], TKEY_CB_MODE_DEFERRED, tkey_event_callback,
              tkey_read_callback, (void *)key2_pin);
    while (1) {
        tkey_dispatch(8);
    }
    return 0;
}
```

## Design

tickey uses a polling-based key scanning mechanism.

The application periodically calls:

```c
tkey_scan(keys, key_count);
```

For example, every 10 ms.

Internally, each key maintains:

* State machine
* Debounce counter
* Long-press counter
* Multi-press counter

During each scan cycle, the library reads the current key state through the user-provided callback:

```c
read_value = key->read_cb(key->user_data);
```

The state machine is then updated and corresponding events are generated.

---

### State Machine

tickey uses a lightweight finite state machine (FSM).

```text
                     debounce
              +--------------------+
              |                    |
              v                    |
       +-------------+             |
       | UNPRESSED   |             |
       +-------------+             |
              |                    |
              | PRESS /            |
              | MULTI_PRESS        |
              v                    |
       +-------------+             |
       |  PRESSED    |-------------+
       +-------------+
              |
              | release
              v
       +-------------+
       | UNPRESSED   |
       +-------------+
```

Long press is **not** a separate state, but an event raised in the PRESSED state:

* When `press_ticks == long_press_duration_ticks`, `TKEY_EVENT_LONG_PRESS` is raised (state unchanged)
* Releasing during a long press raises `TKEY_EVENT_LONG_RELEASE`

#### States

| State      | Description              |
| ---------- | ------------------------ |
| UNPRESSED  | Key is released          |
| PRESSED    | Key is currently pressed |

#### Events

| Event                    | Description                  |
| ------------------------ | ---------------------------- |
| TKEY_EVENT_PRESS         | First press                  |
| TKEY_EVENT_RELEASE       | Release after first press    |
| TKEY_EVENT_LONG_PRESS    | Long-press threshold reached |
| TKEY_EVENT_LONG_RELEASE  | Release after long press     |
| TKEY_EVENT_MULTI_PRESS   | Second or subsequent press   |
| TKEY_EVENT_MULTI_RELEASE | Release after multi-press    |
| TKEY_EVENT_PRESS_TIMEOUT | Click sequence timed out while pressed  |
| TKEY_EVENT_RELEASE_TIMEOUT | Click sequence timed out while released |

---

### Timeout Events

tickey maintains two independent counters:

```text
press_ticks
    └── debounce and long-press detection

multi_press_ticks
    └── multi-press timeout detection
```

When:

```c
multi_press_ticks >= multi_press_timeout_ticks
```

a timeout event is generated.

| Current State | Generated Event            |
| ------------- | -------------------------- |
| PRESSED       | TKEY_EVENT_PRESS_TIMEOUT   |
| UNPRESSED     | TKEY_EVENT_RELEASE_TIMEOUT |

Timeout events do not change the FSM state.

Instead, they indicate that the current click sequence has finished.

Users typically determine single-click, double-click, or triple-click actions using:

```c
TKEY_EVENT_RELEASE_TIMEOUT
```

Example:

```c
if (event & TKEY_EVENT_RELEASE_TIMEOUT) {
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

---

### Callback Execution Model

tickey supports two callback modes.

#### Immediate Mode

```c
TKEY_CB_MODE_IMMEDIATE
```

The callback is executed immediately after the event is generated.

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
TKEY_CB_MODE_DEFERRED
```

Events are first pushed into the internal queue.

```text
Scan
  ↓
Generate Event
  ↓
Push Queue
```

They are later processed by:

```c
tkey_dispatch()
```

```text
Dispatch
  ↓
Execute Callback
```

Advantages:

* Suitable for interrupt-driven scanning
* Callbacks run in task or main-loop context
* Prevents lengthy callbacks from affecting scan timing

---

### Concurrency Model

tickey uses an SPSC (Single Producer Single Consumer) model.

**Producer**

```c
tkey_scan()
```

**Consumer**

```c
tkey_dispatch()
```

The internal event queue is protected through a platform-specific lock abstraction.

Users can implement locking by overriding:

```c
static inline int tkey_lock(void)
{
    /* Disable interrupts if needed */
    return 0;
}

static inline void tkey_unlock(int tkey_lock_state)
{
    /* Restore interrupt state */
    (void)tkey_lock_state;
}
```

The following APIs are internally protected by the lock abstraction and are safe to call from any execution context, provided that `tkey_lock()`/`tkey_unlock()` are implemented correctly:

* `tkey_set_debounce()`
* `tkey_set_long_press_duration()`
* `tkey_set_multi_press_timeout()`

The following APIs must follow the SPSC model: only one execution context may call each function at a time:

* `tkey_scan()`
* `tkey_dispatch()`

## API Reference

### tkey_init

```c
int tkey_init(tkey_t *key,
              tkey_cb_mode_t cb_mode,
              tkey_event_cb_t event_cb,
              tkey_read_cb_t read_cb,
              void *user_data);
```

Initialize a key object.

**Parameters**

* `key` - Key object
* `cb_mode` - Callback mode
* `event_cb` - Event callback
* `read_cb` - Read callback
* `user_data` - User-defined data

**Return Value**

* `0` - Success
* `-TKEY_EINVAL` - Invalid parameter

---

### tkey_scan

```c
int tkey_scan(tkey_t keys[], uint32_t key_count);
```

Scan key states.

**Parameters**

* `keys` - Key object array
* `key_count` - Number of keys

**Return Value**

* `0` - Success
* `-TKEY_EINVAL` - Invalid parameter
* `-TKEY_EAGAIN` - Queue full (when multiple keys fail to enqueue, the error codes are combined with bitwise OR)

---

### tkey_dispatch

```c
void tkey_dispatch(uint8_t max_event_num);
```

Process queued events and execute callbacks.

Only valid in deferred mode.

**Parameters**

* `max_event_num` - Maximum events processed per call

---

### tkey_set_debounce

```c
int tkey_set_debounce(tkey_t *key,
                      uint16_t debounce_ticks);
```

Set debounce duration.

---

### tkey_set_long_press_duration

```c
int tkey_set_long_press_duration(
    tkey_t *key,
    uint16_t long_press_duration_ticks);
```

Set long-press duration.

---

### tkey_set_multi_press_timeout

```c
int tkey_set_multi_press_timeout(
    tkey_t *key,
    uint16_t multi_press_timeout_ticks);
```

Set multi-press timeout interval.

## Macros

### TKEY_DEFAULT_DEBOUNCE

Default debounce duration. Default: `1`

### TKEY_DEFAULT_LONG_PRESS_THRESHOLD

Default long-press duration. Default: `50`

### TKEY_DEFAULT_MULTI_PRESS_INTERVAL

Default multi-press timeout. Default: `30`

### TKEY_QUEUE_SIZE

Event queue length.

Requirements:

* Must be a power of two
* Must not exceed 256

Default: `16`

### TKEY_MAX_TICKS

Maximum tick counter value.

### TKEY_MAX_COUNT

Maximum press count.

### TKEY_EINVAL

Invalid parameter error code.

### TKEY_EAGAIN

Queue full error code.
