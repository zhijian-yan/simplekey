<h1 align="center">tickey</h1>

<p align="center">
<a href="README.md">English</a> | <a href="README_zh.md">简体中文</a>
</p>

<p align="center">
轻量级嵌入式按键扫描库
</p>

## Features

* 支持按键消抖
* 支持检测短按、长按、多次按下等状态
* 支持延迟回调与立即回调
* SPSC (单生产者单消费者) 事件队列
* 无动态内存分配
* 平台无关的锁抽象

## 安装

### Git Submodule

```bash
git submodule add https://github.com/zhijian-yan/tickey.git
```

### 直接集成

将以下文件加入工程：

* `tickey.c`
* `tickey.h`

## 快速开始

### 1. 创建按键

```c
tkey_t key;
```

### 2. 初始化按键

```c
tkey_init(&key, TKEY_CB_MODE_DEFERRED, tkey_event_callback,
          tkey_read_callback, NULL);
```

### 3. 实现读取回调

```c
int tkey_read_callback(void *user_data) {
    if (gpio_get_level((int)user_data) == PRESSED_LEVEL)
        return 1;
    else
        return 0;
}
```

### 4. 实现事件回调

```c
void tkey_event_callback(tkey_t *key, tkey_event_t event, uint8_t press_count,
                         void *user_data) {
    if (event & TKEY_EVENT_LONG_PRESS)
        printf("key[%d] long pressed\r\n", (int)user_data);
    else if (event & TKEY_EVENT_RELEASE_TIMEOUT)
        printf("key[%d] pressed:[%d]\r\n", (int)user_data, press_count);
}
```

### 5. 扫描按键

```c
void timer_callback(void) {
    tkey_scan(&key, 1);
}
```

### 6. 分发按键事件

```c
while (1) {
    tkey_dispatch(8);
}
```

### 7. 完整示例

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

## 设计原理

tickey 采用周期扫描（Polling）方式实现按键检测

用户需要以固定周期调用：

```c
tkey_scan(keys, key_count);
```

例如每 10ms 调用一次

库内部维护：

* 按键状态机
* 消抖计数器
* 长按计数器
* 多击计数器

每次扫描时首先通过用户提供的读取回调获取按键电平：

```c
read_value = key->read_cb(key->user_data);
```

随后根据状态机更新按键状态并生成对应事件

---

### 状态机

tickey 采用两状态有限状态机（FSM）设计：

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

长按不是独立状态，而是 PRESSED 状态下的一个事件：

* 当 `press_ticks == long_press_duration_ticks` 时触发 `TKEY_EVENT_LONG_PRESS`（状态不变）
* 长按期间释放时触发 `TKEY_EVENT_LONG_RELEASE`

状态说明：

| 状态        | 描述      |
| ---------- | ------- |
| UNPRESSED  | 按键未按下   |
| PRESSED    | 按键已按下   |

对应事件：

| 事件                       | 描述       |
| ------------------------ | -------- |
| TKEY_EVENT_PRESS         | 第一次按下    |
| TKEY_EVENT_RELEASE       | 单击释放     |
| TKEY_EVENT_LONG_PRESS    | 达到长按阈值   |
| TKEY_EVENT_LONG_RELEASE  | 长按后释放    |
| TKEY_EVENT_MULTI_PRESS   | 第二次及以上按下 |
| TKEY_EVENT_MULTI_RELEASE | 多击释放     |
| TKEY_EVENT_PRESS_TIMEOUT | 按下期间点击序列超时 |
| TKEY_EVENT_RELEASE_TIMEOUT | 释放期间点击序列超时 |

---

### 超时事件

库内部同时维护：

```text
press_ticks
    └── 消抖和长按检测

multi_press_ticks
    └── 多击超时检测
```

当：

```c
multi_press_ticks >= multi_press_timeout_ticks
```

时产生超时事件：

| 当前状态      | 产生事件                       |
| --------- | -------------------------- |
| PRESSED   | TKEY_EVENT_PRESS_TIMEOUT   |
| UNPRESSED | TKEY_EVENT_RELEASE_TIMEOUT |

超时事件不会改变状态机状态，仅用于通知用户当前点击序列已经结束

通常用户可在：

```c
TKEY_EVENT_RELEASE_TIMEOUT
```

事件中判断：

* 单击
* 双击
* 三击
* 多次连续点击

例如：

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

### 回调执行模型

tickey 支持两种回调模式。

#### Immediate Mode

```c
TKEY_CB_MODE_IMMEDIATE
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
* 无事件队列

#### Deferred Mode

```c
TKEY_CB_MODE_DEFERRED
```

事件首先进入内部队列：

```text
Scan
  ↓
Generate Event
  ↓
Push Queue
```

随后由 `tkey_dispatch()` 统一分发：

```text
Dispatch
  ↓
Execute Callback
```

特点：

* 适合在中断中执行扫描
* 回调运行在主循环或任务上下文
* 避免耗时回调影响扫描实时性

---

### 并发模型

tickey 内部采用 `SPSC (Single Producer Single Consumer)` 模型

**生产者**

* tkey_scan()

**消费者**

* tkey_dispatch()

事件队列通过锁抽象保护

tickey 通过两个接口抽象平台相关的锁实现：

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

以下 API 内部使用锁保护，在正确实现 `tkey_lock()`/`tkey_unlock()` 的前提下可在任意执行上下文中调用：

* `tkey_set_debounce()`
* `tkey_set_long_press_duration()`
* `tkey_set_multi_press_timeout()`

以下 API 必须遵循 SPSC 模型，即同一时刻只能由一个执行上下文调用：

* `tkey_scan()`
* `tkey_dispatch()`

## API参考

### tkey_init

```c
int tkey_init(tkey_t *key,
              tkey_cb_mode_t cb_mode,
              tkey_event_cb_t event_cb,
              tkey_read_cb_t read_cb,
              void *user_data);
```

初始化按键

**参数**

* `key`：按键对象
* `cb_mode`：回调执行模式
* `event_cb`：事件回调函数
* `read_cb`：读取回调函数
* `user_data`：传递给回调函数的用户数据

**返回值**

* `0`：成功
* `-TKEY_EINVAL`：参数非法

---

### tkey_scan

```c
int tkey_scan(tkey_t keys[], uint32_t key_count);
```

扫描按键状态

* 对于 `TKEY_CB_MODE_DEFERRED`，产生事件并放入队列
* 对于 `TKEY_CB_MODE_IMMEDIATE`，直接执行回调

**参数**

* `keys`：按键对象数组
* `key_count`：按键对象数量

**返回值**

* `0`：成功
* `-TKEY_EINVAL`：参数非法
* `-TKEY_EAGAIN`：队列已满（多个按键入队失败时错误码按位或合并）

---

### tkey_dispatch

```c
void tkey_dispatch(uint8_t max_event_num);
```

处理按键事件队列并执行回调

仅对 `TKEY_CB_MODE_DEFERRED` 模式有效

**参数**

* `max_event_num`：单次调用处理事件的最大数量

---

### tkey_set_debounce

```c
int tkey_set_debounce(tkey_t *key, uint16_t debounce_ticks);
```

设置按键的消抖时间

**参数**

* `key`：按键对象
* `debounce_ticks`：消抖时间

**返回值**

* `0`：成功
* `-TKEY_EINVAL`：参数非法

---

### tkey_set_long_press_duration

```c
int tkey_set_long_press_duration(tkey_t *key,
                                 uint16_t long_press_duration_ticks);
```

设置按键的长按持续时间

**参数**

* `key`：按键对象
* `long_press_duration_ticks`：长按持续时间

**返回值**

* `0`：成功
* `-TKEY_EINVAL`：参数非法

---

### tkey_set_multi_press_timeout

```c
int tkey_set_multi_press_timeout(tkey_t *key,
                                 uint16_t multi_press_timeout_ticks);
```

设置按键的多次按下间隔超时时间

**参数**

* `key`：按键对象
* `multi_press_timeout_ticks`：多次按下间隔超时时间

**返回值**

* `0`：成功
* `-TKEY_EINVAL`：参数非法

## 宏

### TKEY_DEFAULT_DEBOUNCE

默认的消抖时间，默认值：`1`

### TKEY_DEFAULT_LONG_PRESS_THRESHOLD

默认的长按持续时间，默认值：`50`

### TKEY_DEFAULT_MULTI_PRESS_INTERVAL

默认的多次按下间隔时间，默认值：`30`

### TKEY_QUEUE_SIZE

队列长度

要求：

* 必须为 2 的幂
* 不得超过 256

默认值：`16`

### TKEY_MAX_TICKS

按键最大计时数

### TKEY_MAX_COUNT

按键最大按下次数

### TKEY_EINVAL

参数非法错误码

### TKEY_EAGAIN

队列已满错误码
