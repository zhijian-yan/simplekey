<h1 align="center">simplekey</h1>

<p align="center">
<a href="README.md">English</a> | <a href="README_zh.md">简体中文</a>
</p>

<p align="center">
轻量级嵌入式按键扫描库
</p>

## 特性

* 分组式按键管理，同一分组内的按键共享回调、配置与事件队列
* 信号层 + 手势层双层状态机
* 按下与释放消抖模式可独立配置（立即 / 延迟）
* 支持消抖、长按、多击与超时检测
* 支持立即回调与延迟回调
* 基于用户提供缓冲区的 SPSC 事件队列
* 无动态内存分配
* 平台无关的锁抽象

## 安装

### Git Submodule

```bash
git submodule add https://github.com/zhijian-yan/simplekey.git
```

### 直接集成

将以下文件加入工程：

* `simplekey.c`
* `simplekey.h`

## 快速开始

### 1. 定义按键、缓冲区与配置

```c
#define KEY_NUM 2

skey_t keys[KEY_NUM];
skey_group_t group;
skey_group_config_t config;         /* 用于初始化 group，可以是局部变量 */
skey_message_t queue_buffer[16];    /* queue_size 必须是 2 的幂 */
```

`skey_t` 与 `skey_group_t` 由 `skey_init_key()` 与 `skey_init_group()` 初始化，因此无需事先清零。`config` 是只读的普通值，可以放在栈上

### 2. 配置并初始化分组

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

config.queue_buffer = queue_buffer; /* 延迟模式必需 */
config.queue_size = 16;             /* queue_buffer 的元素个数 */

skey_init_key(&keys[0], (void *)KEY1_PIN);
skey_init_key(&keys[1], (void *)KEY2_PIN);
skey_init_group(&group, &config);
```

`config` 的每个字段都会被拷贝进 `group`，因此 `skey_init_group()` 返回后 `config` 无需继续存在。在 `SKEY_CALLBACK_MODE_DEFERRED` 模式下 `queue_buffer` 与 `queue_size` 是必需的；在 `SKEY_CALLBACK_MODE_IMMEDIATE` 模式下会被忽略，可以保持为 `0`

### 3. 实现读取回调

```c
uint8_t skey_read_cb(void *user_data) {
    /* 返回 0 表示按下，非 0 表示释放 */
    if (gpio_get_level((int)user_data) == 0)
        return 0;
    return 1;
}
```

### 4. 实现事件回调

```c
void skey_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    if (events & SKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        printf("key[%d] pressed:%d\r\n", (int)user_data, press_count);
}
```

`events` 为位掩码，同一次回调中可能包含多个事件，请使用按位与进行判断

### 5. 周期扫描按键

```c
void timer_callback(void) {
    /* 建议以固定周期调用，例如每 10ms 一次 */
    skey_scan(keys, KEY_NUM, &group);
}
```

### 6. 分发事件

```c
while (1) {
    skey_dispatch(8, &group);
}
```

仅 `SKEY_CALLBACK_MODE_DEFERRED` 模式需要调用 `skey_dispatch()`

### 7. 完整示例

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

## 设计原理

simplekey 采用周期扫描（Polling）方式实现按键检测

用户需要以固定周期调用：

```c
skey_scan(keys, key_count, &group);
```

例如每 10ms 调用一次

### 分组模型

按键以“分组（group）”为单位进行管理

* `skey_t` 描述单颗按键的运行时状态
* `skey_group_t` 描述一组按键共享的读取回调、事件回调、消抖模式、时序阈值与事件队列

一次 `skey_scan()` 调用会对传入数组中的每颗按键依次执行：

1. 通过 `group->read_cb()` 读取电平
2. 信号层状态机进行采样与消抖
3. 手势层状态机进行长按、多击与超时识别
4. 产生事件并交由回调或事件队列处理

```text
             skey_scan()
                  │
        ┌─────────┴─────────┐
        │  逐颗按键依次处理   │
        ▼                   ▼
    信号层 FSM          手势层 FSM
  （采样 + 消抖）     （长按 / 多击 / 超时）
        │                   │
        └─────────┬─────────┘
                  ▼
              事件 event
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
   立即回调             事件队列
 (IMMEDIATE)              │
                          ▼
                  skey_dispatch()
                          │
                          ▼
                      延迟回调
```

### 状态编码

单颗按键的全部状态被编码在一个字节 `state` 中：

| 位    | 字段      | 说明                                                         |
| ---- | ------- | ---------------------------------------------------------- |
| 0-2  | 信号层状态   | `IDLE` / `PRESS_DEBOUNCE` / `PRESSED` / `RELEASE_DEBOUNCE` / `RELEASED` |
| 3-4  | 手势层状态   | `IDLE` / `PRESSED` / `RELEASED`                            |
| 5    | `LONG_PRESSED`  | 已触发长按                                                      |
| 6    | `MULTI_PRESSED` | 多击序列进行中                                                    |

### 信号层状态机

信号层负责按键采样与消抖，输入为用户读取回调返回的电平（`0` 表示按下，非 `0` 表示释放）

```text
          level == 0
   IDLE ─────────────▶ PRESS_DEBOUNCE
    ▲                        │ 消抖确认
    │                        ▼
    │                     PRESSED
    │                        │ level != 0
    │                        ▼
    └──── RELEASED ◀──── RELEASE_DEBOUNCE
               消抖确认
```

| 当前状态             | 条件                                                       | 动作                                        |
| ---------------- | -------------------------------------------------------- | ----------------------------------------- |
| `IDLE`           | `level == 0`                                             | `ticks = 0`，→ `PRESS_DEBOUNCE`，产生 `PRESS_EAGER` |
| `PRESS_DEBOUNCE` | `ticks >= press_debounce_ticks` 且 `level == 0`           | `ticks = 0`，→ `PRESSED`，产生 `PRESS_DEFER`  |
| `PRESS_DEBOUNCE` | `ticks >= press_debounce_ticks` 且 `level != 0`           | `key->state = 0`，判定本次按下无效（抖动）                   |
| `PRESS_DEBOUNCE` | 其它                                                       | `ticks++`                                 |
| `PRESSED`        | `level != 0`                                             | `ticks = 0`，→ `RELEASE_DEBOUNCE`，产生 `RELEASE_EAGER` |
| `PRESSED`        | 其它                                                       | `ticks++`（上限 `SKEY_MAX_TICK`）             |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` 且 `level != 0`        | `ticks = 0`，→ `RELEASED`，产生 `RELEASE_DEFER` |
| `RELEASE_DEBOUNCE` | `ticks >= release_debounce_ticks` 且 `level == 0`        | `key->state = 0`，判定本次按下无效（抖动）                   |
| `RELEASE_DEBOUNCE` | 其它                                                      | `ticks++`                                 |
| `RELEASED`       | `level == 0`                                             | `ticks = 0`，手势层→ `IDLE`，→ `PRESS_DEBOUNCE`，产生 `PRESS_EAGER` |
| `RELEASED`       | 其它                                                       | `ticks++`                                 |

#### 抖动判定与状态清除（跳变无效）

抖动是指电平在消抖确认之前回到了先前的值。若抖动在消抖后依然成立，则判定为触点噪声，本次按下整体无效：整个 `state` 字节被清零，等同于这次按下从未发生。

清零会在一次操作中同时复位信号层状态、手势层状态与手势层标志位。手势层不需要额外通知：它的状态本就存放在同一个字节里，随之一并清除，因此两层天然保持一致。

这个取舍是刻意为之：手势层无法区分"抖动"与"被打断的按压"，所以已经在进行的序列会连同抖动一起被丢弃。由此带来两个后果，且仅在对应的消抖阈值大于 `1` tick 时才会出现：

* **抬手抖动**：已经上报过的长按（`LONG_PRESS`）不会再有配对的 `LONG_RELEASE`，该按压序列被丢弃而不是走完
* **抬手抖动**：点击序列同样被丢弃，因此 [计时](#计时) 一节推荐的 `MULTI_RELEASE_TIMEOUT`（用于判定单击/双击）不会产生。`release_debounce_ticks = 2` 时，第 2 次抬手出现抖动的双击仍能累计到 `press_count == 2`，但超时事件不会到来，这次点击不会被上报
* **按下抖动**：这一种无害。手势层此时尚未承诺这次按下，之后的按下会被正常跟踪，仍计为一次单击

代价是以"整段序列"换取抖动抵抗能力，而不是只丢弃抖动本身。如果依赖 `LONG_RELEASE` 或依赖 `MULTI_RELEASE_TIMEOUT` 做点击计数，请将 `release_debounce_ticks` 保持为 `1`，或者在应用层把被判无效的序列按丢失处理。按下抖动不会使已在跟踪的序列失效，因此 `press_debounce_ticks` 可以放心调大

### 手势层状态机

手势层接收信号层事件，结合消抖模式进行手势识别，并输出长按、多击与超时事件

```text
   IDLE ──(按下事件)──▶ PRESSED ──(释放事件)──▶ RELEASED
    ▲                                                │
    │            序列结束（复位）                       │
    └────────────────────────────────────────────────┘
```

| 当前状态       | 条件                                                       | 动作                                                                              |
| ---------- | -------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `IDLE`     | 按下事件（依 `press_debounce_mode` 选择 `PRESS_DEFER` / `PRESS_EAGER`） | → `PRESSED`；若 `MULTI_PRESSED` 已置位则 `press_count++`，否则 `press_count = 1` 并置位 `MULTI_PRESSED` |
| `PRESSED`  | 释放事件（依 `release_debounce_mode` 选择 `RELEASE_DEFER` / `RELEASE_EAGER`） | → `RELEASED`                                                                    |
| `PRESSED`  | 未置位 `LONG_PRESSED` 且 `ticks > long_press_expired_ticks`   | 置位 `LONG_PRESSED`，产生 `LONG_PRESS`                                               |
| `PRESSED`  | 置位 `MULTI_PRESSED` 且 `ticks > multi_press_timeout_ticks`  | 清除 `MULTI_PRESSED`，产生 `MULTI_PRESS_TIMEOUT`                                     |
| `RELEASED` | 置位 `LONG_PRESSED`                                        | 清除 `LONG_PRESSED`，产生 `LONG_RELEASE`                                             |
| `RELEASED` | 置位 `MULTI_PRESSED` 且 `ticks > multi_release_timeout_ticks` | 清除 `MULTI_PRESSED`，产生 `MULTI_RELEASE_TIMEOUT`                                   |
| `RELEASED` | 未置位 `MULTI_PRESSED` 且信号层处于 `RELEASED`                   | 复位整个 `state`（点击序列结束）                                                           |

### 消抖模式

信号层在采样到电平跳变时以及消抖确认时会分别产生事件，手势层选择在哪个时机响应由消抖模式决定

| 模式                            | 含义                                |
| ----------------------------- | --------------------------------- |
| `SKEY_DEBOUNCE_MODE_DEFER`    | 在消抖确认后响应（更抗抖动）                    |
| `SKEY_DEBOUNCE_MODE_EAGER`    | 在检测到电平跳变时立即响应（延迟更低，抗抖动能力较弱）        |

按下与释放可分别通过 `config.press_debounce_mode` 与 `config.release_debounce_mode` 独立配置，二者由 `skey_init_group()` 拷贝进分组

### 事件

| 事件                            | 说明                                                       |
| ----------------------------- | -------------------------------------------------------- |
| `SKEY_EVENT_PRESS_EAGER`      | 检测到按键电平变为“按下”（消抖确认前）                                |
| `SKEY_EVENT_PRESS_DEFER`      | 按下消抖确认完成                                                 |
| `SKEY_EVENT_RELEASE_EAGER`    | 检测到按键电平变为“释放”（消抖确认前）                                |
| `SKEY_EVENT_RELEASE_DEFER`    | 释放消抖确认完成                                                 |
| `SKEY_EVENT_LONG_PRESS`       | 按下持续时间超过 `long_press_expired_ticks`                     |
| `SKEY_EVENT_LONG_RELEASE`     | 长按之后释放                                                   |
| `SKEY_EVENT_MULTI_PRESS_TIMEOUT` | 按下期间，多击等待超过 `multi_press_timeout_ticks`                 |
| `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` | 释放期间，多击等待超过 `multi_release_timeout_ticks`             |

多个事件可能在同一次回调中同时产生，`event` 为多个事件的按位或结果

### 计时

`key->ticks` 在按下期间（`PRESSED`）与释放期间（`RELEASED`）累加，用于长按与超时判断

```text
ticks
  ├── 按下期间累加
  │     ├── long_press_expired_ticks     → 长按
  │     └── multi_press_timeout_ticks    → 按下期间多击超时
  │
  └── 释放期间累加
        └── multi_release_timeout_ticks  → 释放期间多击超时
```

通常用户在 `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` 事件中结合 `press_count` 判断单击、双击或多次点击：

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

### 回调执行模型

simplekey 支持两种回调模式

#### Immediate Mode

```c
SKEY_CALLBACK_MODE_IMMEDIATE
```

事件产生后立即执行回调

```text
Scan
  ↓
Generate Event
  ↓
Execute Callback
```

特点：

* 延迟最低
* 不使用事件队列

#### Deferred Mode

```c
SKEY_CALLBACK_MODE_DEFERRED
```

事件首先进入分组的事件队列：

```text
Scan
  ↓
Generate Event
  ↓
Push Queue
```

随后由 `skey_dispatch()` 统一分发：

```text
Dispatch
  ↓
Execute Callback
```

特点：

* 适合在中断中执行扫描
* 回调运行在主循环或任务上下文
* 避免耗时回调影响扫描实时性

### 事件队列

分组内的事件通过单生产者单消费者（SPSC）环形队列传递，缓冲区由用户提供：

```c
typedef struct {
    skey_message_t *buffer;   /* 用户提供的缓冲区，元素个数为 queue_size */
    uint8_t capacity;         /* 下标掩码 = queue_size - 1，由 skey_init_group() 设置 */
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

`capacity` 是 `skey_init_group()` 维护的下标掩码：`buffer` 有 `capacity + 1` 个元素，队列最多可容纳 `capacity` 条消息。环形队列始终空出一个槽位来区分"满"与"空"，因此 `queue_size = 16` 的队列可容纳 15 条消息。

要求：

* `config.queue_size` 是 `queue_buffer` 的元素个数：必须是 2 的幂，取值在 `SKEY_MIN_QUEUE_SIZE`（`2`）与 `SKEY_MAX_QUEUE_SIZE`（`256`）之间
* 在 `SKEY_CALLBACK_MODE_IMMEDIATE` 模式下队列不使用，这些字段可保持为 `0`
* 队列满时事件将被丢弃，`skey_scan()` 的返回值会累加丢弃数量

### 并发模型

simplekey 内部采用 `SPSC (Single Producer Single Consumer)` 模型

**生产者**

* `skey_scan()`

**消费者**

* `skey_dispatch()`

事件队列由 SPSC 模型保证安全，`skey_scan()` 使用锁抽象保护按键状态（含信号层状态、手势层状态与标志位）的更新过程

临界区仅覆盖状态更新。事件入队与 `event_cb` 调用均在 `skey_unlock()` 之后进行，依赖上述 SPSC 模型而非锁来保证安全

simplekey 通过两个接口抽象平台相关的锁实现：

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

默认实现为空操作，需要中断安全的平台可自行实现这两个接口

以下 API 必须遵循 SPSC 模型，即同一时刻只能由一个执行上下文调用：

* `skey_scan()`
* `skey_dispatch()`

## API 参考

### skey_init_key

```c
void skey_init_key(skey_t *key, void *user_data);
```

初始化单个按键并绑定其用户数据

**参数**

* `key`：待初始化的按键对象，不可为空
* `user_data`：该按键在 `read_cb` 与 `event_cb` 中回传的用户数据，可为空

整个按键状态会被清零，因此对未初始化的对象调用也是安全的

---

### skey_init_group

```c
void skey_init_group(skey_group_t *group, const skey_group_config_t *config);
```

按配置初始化分组

**参数**

* `group`：待初始化的分组对象，不可为空；原有内容会被丢弃
* `config`：待拷贝的配置，不可为空，且不会被修改

**说明**

* `config->read_cb` 与 `config->event_cb` 均不可为空
* 在 `SKEY_CALLBACK_MODE_DEFERRED` 模式下，`config->queue_buffer` 不可为空，且 `config->queue_size` 必须是 2 的幂，取值在 `SKEY_MIN_QUEUE_SIZE`（`2`）与 `SKEY_MAX_QUEUE_SIZE`（`256`）之间
* 在 `SKEY_CALLBACK_MODE_IMMEDIATE` 模式下，队列相关字段会被忽略
* 所有字段都会被拷贝，因此 `config` 可以是之后即失效的局部变量

---

### skey_scan

```c
int skey_scan(skey_t keys[], uint8_t key_count, skey_group_t *group);
```

扫描按键状态

* 对于 `SKEY_CALLBACK_MODE_IMMEDIATE`，事件产生后直接执行回调
* 对于 `SKEY_CALLBACK_MODE_DEFERRED`，事件被放入分组的事件队列

**参数**

* `keys`：按键对象数组，不可为空，且必须已由 `skey_init_key()` 初始化
* `key_count`：按键对象数量
* `group`：按键所属分组，不可为空

**返回值**

* `int`：因队列已满而未能入队的事件数量（立即模式恒为 `0`）；若需要感知事件被丢弃，请检查该返回值

---

### skey_dispatch

```c
void skey_dispatch(uint8_t max_event_count, skey_group_t *group);
```

处理分组事件队列并执行回调

仅对 `SKEY_CALLBACK_MODE_DEFERRED` 模式有效

**参数**

* `max_event_count`：单次调用处理事件的最大数量
* `group`：按键所属分组

---

### skey_lock / skey_unlock

```c
static inline int skey_lock(void);
static inline void skey_unlock(int skey_lock_state);
```

平台相关的锁抽象

`skey_lock()` 返回的锁状态会传给 `skey_unlock()`，用于恢复临界区

默认实现为空操作，需要中断安全的平台可自行实现

## 数据结构

### skey_t

```c
typedef struct {
    volatile uint16_t ticks;
    volatile uint8_t press_count;
    volatile uint8_t state;
    void *user_data;
} skey_t;
```

* `ticks`：计时器，用于消抖、长按与超时判断
* `press_count`：当前点击序列内的按下次数
* `state`：编码后的信号层状态、手势层状态与标志位
* `user_data`：传递给读取/事件回调的用户数据

请使用 `skey_init_key()` 创建，它会清零整个对象并绑定用户数据。内部字段由库维护，不应直接写入

### skey_message_t

```c
typedef struct {
    uint8_t events;
    uint8_t press_count;
    void *user_data;
} skey_message_t;
```

事件队列中的消息单元

### skey_queue_t

```c
typedef struct {
    skey_message_t *buffer;
    uint8_t capacity;
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} skey_queue_t;
```

SPSC 事件队列

`capacity` 是 `buffer` 的元素个数，也就是 `config.queue_size`。它不是队列能容纳的消息数量：环形队列始终空出一个槽位来区分"满"与"空"，因此 `queue_size = 16` 构建的队列最多容纳 `15` 条消息。该字段由 `skey_init_group()` 设置

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

供 `skey_init_group()` 使用的配置。它只被读取一次并拷贝进分组，因此可以是局部变量，也不必事先清零——只要下面用到的字段都被赋值即可

* `read_cb` / `event_cb`：回调，二者均为必需
* `callback_mode`：`SKEY_CALLBACK_MODE_DEFERRED` 或 `SKEY_CALLBACK_MODE_IMMEDIATE`
* `queue_buffer` / `queue_size`：队列存储与其元素个数（2 的幂，取值在 `SKEY_MIN_QUEUE_SIZE` 与 `SKEY_MAX_QUEUE_SIZE` 之间）；延迟模式必需，立即模式忽略
* `press_debounce_mode` / `release_debounce_mode`：手势层如何响应信号层，`SKEY_DEBOUNCE_MODE_DEFER` 等消抖确认，`SKEY_DEBOUNCE_MODE_EAGER` 在电平跳变时立即响应
* `press_debounce_ticks` / `release_debounce_ticks`：消抖时间，至少为 `1`
* `long_press_expired_ticks`：长按判定阈值
* `multi_press_timeout_ticks`：按下期间多击超时
* `multi_release_timeout_ticks`：释放期间多击超时

本库不做默认值填充：字段留 `0` 就保持 `0`，因此请为每个依赖的字段赋值

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

按键分组配置

分组不是按键的集合：它保存一组按键共享的回调、配置与事件队列。按键自身是作为 `skey_t` 数组单独传给 `skey_scan()` 的，因此这里的 `group` 指"这些按键共享的配置"，而不是"该分组内的按键"。

* `read_cb`：读取回调，返回 `0` 表示按下，非 `0` 表示释放
* `event_cb`：事件回调
* `callback_mode`：回调执行模式
* `press_debounce_mode` / `release_debounce_mode`：按下 / 释放消抖模式
* `press_debounce_ticks` / `release_debounce_ticks`：按下 / 释放消抖时间
* `long_press_expired_ticks`：长按判定阈值
* `multi_press_timeout_ticks`：按下期间多击超时
* `multi_release_timeout_ticks`：释放期间多击超时
* `queue`：事件队列

请使用 `skey_init_group()` 创建，而不是手工填充字段

## 宏与枚举

### 事件宏

均为位掩码：

| 宏                                  | 值          |
| ---------------------------------- | ---------- |
| `SKEY_EVENT_PRESS_DEFER`           | `1U << 0`  |
| `SKEY_EVENT_PRESS_EAGER`           | `1U << 1`  |
| `SKEY_EVENT_RELEASE_DEFER`         | `1U << 2`  |
| `SKEY_EVENT_RELEASE_EAGER`         | `1U << 3`  |
| `SKEY_EVENT_LONG_PRESS`            | `1U << 4`  |
| `SKEY_EVENT_LONG_RELEASE`          | `1U << 5`  |
| `SKEY_EVENT_MULTI_PRESS_TIMEOUT`   | `1U << 6`  |
| `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` | `1U << 7`  |

辅助宏：

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

回调执行模式

### skey_debounce_mode_t

```c
typedef enum {
    SKEY_DEBOUNCE_MODE_DEFER = 0,
    SKEY_DEBOUNCE_MODE_EAGER,
} skey_debounce_mode_t;
```

消抖模式

### SKEY_MAX_TICK

计时器上限，值为 `0xFFFF`（`65535`）

达到该值后 `ticks` 不再增加

### SKEY_MAX_COUNT

按下次数上限，值为 `0xFF`（`255`）

达到该值后 `press_count` 不再增加

### SKEY_MAX_QUEUE_SIZE

队列缓冲区元素个数上限，值为 `256`

由于环形队列始终空出一个槽位，这样的队列最多容纳 255 条消息

### SKEY_MIN_QUEUE_SIZE

队列缓冲区元素个数下限，值为 `2`

`2` 个元素的队列最多容纳 1 条消息
