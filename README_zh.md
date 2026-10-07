<h1 align="center">simplekey</h1>

<p align="center">
<a href="README.md">English</a> | <a href="README_zh.md">简体中文</a>
</p>

<p align="center">
轻量级嵌入式按键扫描库
</p>

<p align="center">
<a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square"></a>
<img alt="Language" src="https://img.shields.io/badge/language-C99-blue.svg?style=flat-square">
<img alt="Dependencies" src="https://img.shields.io/badge/dependencies-none-brightgreen.svg?style=flat-square">
<img alt="Dynamic memory" src="https://img.shields.io/badge/dynamic_memory-none-brightgreen.svg?style=flat-square">
<img alt="Platform" src="https://img.shields.io/badge/platform-bare--metal_%7C_RTOS_%7C_Linux-lightgrey.svg?style=flat-square">
</p>

## 特性

* 轮询式扫描，由调用者以固定周期驱动
* 分组式管理，组内按键共享回调、时序配置与事件队列
* 信号层 + 手势层双层状态机
* 按下与释放的消抖模式可独立配置
* 支持消抖、长按、长按后释放、多击与超时事件
* 每次事件都会附带点击计数
* 支持立即回调与延迟回调
* 事件队列缓冲区由用户提供，队列满时丢弃的事件会被计数并返回
* 无动态内存分配，不依赖操作系统，C99
* 平台相关的临界区与顺序屏障由钩子层隔离

## 移植

移植时直接在 `simplekey_port.h` 中实现下列四个平台钩子即可；该文件属于应用，升级库时保留自己的版本

### 平台钩子

| 钩子 | 作用 |
| ---- | ---- |
| `int skey_lock(void)` | 进入临界区，保护单颗按键的状态更新，返回进入前的状态 |
| `void skey_unlock(int state)` | 退出临界区，并恢复 `skey_lock()` 返回的状态 |
| `SKEY_ACQUIRE()` | 读取队列索引之后、读取该索引所发布的事件之前执行的顺序屏障 |
| `SKEY_RELEASE()` | 写入发布事件的队列索引之前执行的顺序屏障 |

### 默认实现

库内提供的钩子默认实现全部为空操作。在 32 位单核平台上，或每个分组只由一个执行上下文驱动时，可以直接使用默认实现，无需编写任何移植代码：库访问的字段都是自然对齐的 32 位及以下宽度，单上下文访问不会被撕裂，也不需要任何顺序屏障。

### 示例：裸机

中断服务函数可能抢占库调用，因此锁需要保存并恢复中断屏蔽状态。单核无需顺序屏障，`SKEY_ACQUIRE()` / `SKEY_RELEASE()` 保持库内默认实现即可。若扫描本身在中断中执行，建议改用 `SKEY_CALLBACK_MODE_DEFERRED`，并在任务或主循环中分发事件，避免在中断上下文执行事件回调。

```c
/* simplekey_port.h */
#include "cmsis_compiler.h" /* CMSIS 5；更低版本请包含 core_cm*.h */

static inline int skey_lock(void) {
    int state = __get_PRIMASK();
    __disable_irq();
    return state;
}

static inline void skey_unlock(int skey_lock_state) {
    __set_PRIMASK(skey_lock_state);
}
```

### 示例：RTOS

应使用 RTOS 的临界区：仅关中断无法覆盖临界区内的任务切换。`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()` 支持嵌套，因此无需保存状态；RT-Thread 可用 `rt_enter_critical()` / `rt_exit_critical()`，或用 `rt_hw_interrupt_disable()` / `rt_hw_interrupt_enable()` 恢复返回的中断级别。单核同样无需顺序屏障。

```c
/* simplekey_port.h */
#include "FreeRTOS.h"
#include "task.h"

static inline int skey_lock(void) {
    taskENTER_CRITICAL();
    return 0;
}

static inline void skey_unlock(int skey_lock_state) {
    (void)skey_lock_state;
    taskEXIT_CRITICAL();
}
```

### 示例：多核

需要所有核共享的锁，以及真正的顺序屏障：`volatile` 本身不保证顺序，否则一个核可能先看到队列索引、后看到该索引发布的事件。取自旋锁前必须先屏蔽本地中断，否则中断一旦抢占持锁上下文就会死锁。

```c
/* simplekey_port.h */
#include "cmsis_compiler.h"

/* skey_spinlock 与 spin_lock() / spin_unlock() 均由 SoC 提供 */
static inline int skey_lock(void) {
    int state = __get_PRIMASK();
    __disable_irq();
    spin_lock(&skey_spinlock);
    return state;
}

static inline void skey_unlock(int skey_lock_state) {
    spin_unlock(&skey_spinlock);
    __set_PRIMASK(skey_lock_state);
}

#define SKEY_ACQUIRE() __DMB()
#define SKEY_RELEASE() __DMB()
```

### 示例：宿主平台（Linux）

做宿主工具、单元测试、仿真或用户态 Linux 驱动时，只要同一个分组只由一个线程使用，直接沿用默认实现即可。多个线程共享一个分组时改用互斥锁；若宿主平台是弱内存模型，还需要真正的屏障。

```c
/* simplekey_port.h */
#include <pthread.h>

static pthread_mutex_t skey_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline int skey_lock(void) {
    pthread_mutex_lock(&skey_mutex);
    return 0;
}

static inline void skey_unlock(int skey_lock_state) {
    (void)skey_lock_state;
    pthread_mutex_unlock(&skey_mutex);
}

#define SKEY_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define SKEY_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)
```

`simplekey_port.h` 会被每个包含库头的编译单元各展开一份，因此上面的互斥锁也是每个编译单元一份：若多个模块都会调用本库，请把互斥锁放到其中一个 `.c` 文件中定义，这里改为 `extern`。

### 注意事项

* 钩子实现需保持无状态，嵌套的加解锁需正确恢复最外层状态：`static inline` 定义对每个编译单元都是私有的，函数内的静态变量会变成每个编译单元一份，而不是整个程序一份
* 临界区只覆盖按键状态更新，事件入队与事件回调均在临界区之外执行

## 使用方法

### 1. 定义对象与回调

```c
#include "simplekey.h"

#define KEY_NUM  2
#define KEY1_PIN 1
#define KEY2_PIN 2

static skey_t keys[KEY_NUM];
static skey_group_t group;
static skey_message_t queue_buffer[16]; /* 延迟模式下必需 */

/* 由事件回调写入，应用自行处理 */
static volatile uint8_t click_count;

extern int gpio_get_level(int pin); /* 应用自己的 GPIO 读取函数 */

/* 返回 0 表示按下；user_data 为按键的引脚号 */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 0 : 1;
}

/* 1 = 单击，2 = 双击，以此类推 */
static void key_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    (void)user_data;

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        click_count = press_count;
}
```

`events` 为若干 `SKEY_EVENT_*` 标志的按位或，一次回调中可能同时携带多个事件，需逐个按位与判断。点击次数在 `SKEY_EVENT_MULTI_RELEASE_TIMEOUT` 中判定，该事件在按键释放超过 `multi_release_timeout_ticks` 后产生

### 2. 初始化按键与分组

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

* `skey_init_key()` 会清零整个按键对象，因此 `user_data` 在此传入，不应再手动赋值
* 配置不做默认值填充：未赋值的字段保持为 0，因此凡是程序依赖的字段都需要显式赋值
* `queue_size` 必须为 2 的幂且位于 `[SKEY_MIN_QUEUE_SIZE, SKEY_MAX_QUEUE_SIZE]` 之间，`queue_buffer` 的元素个数需与之相同；环形队列会空出一个槽位，因此 16 个元素最多缓存 15 个事件
* `SKEY_CALLBACK_MODE_IMMEDIATE` 下队列相关字段被忽略，回调在 `skey_scan()` 中执行，事件不会丢失
* `queue_buffer` 为引用而非拷贝，其生命周期必须覆盖整个分组

### 3. 周期扫描与事件分发

```c
/* 由周期定时器中断调用，例如每 10ms 一次 */
void key_scan_tick(void) {
    (void)skey_scan(keys, KEY_NUM, &group); /* 非 0 表示有事件被丢弃 */
}

for (;;) {
    skey_dispatch(8, &group); /* 仅延迟模式需要 */
    /* 在此处理 click_count */
}
```

`skey_scan()` 的返回值为因事件队列已满而丢弃的事件数，立即模式下恒为 0；同一分组中，`skey_scan()` 与 `skey_dispatch()` 各自只接受一个执行上下文，不同分组之间相互独立。这里扫描在中断中执行、分发在主循环中执行，因此锁钩子必须是中断安全的；若两者都放在主循环中执行，则可直接使用库内默认实现

### 4. 选择时序参数

所有时长都以扫描周期的 tick 计数，若扫描周期为 10ms：

| 字段 | 示例值 | 含义 |
| ---- | ------ | ---- |
| `press_debounce_ticks` | 1 | 采到按下电平后约 10ms 确认按下 |
| `release_debounce_ticks` | 1 | 采到释放电平后约 10ms 确认释放 |
| `long_press_expired_ticks` | 100 | 确认按下后再过 100 个 tick（约 1s）上报长按 |
| `multi_press_timeout_ticks` | 30 | 确认按下后再过 30 个 tick（约 300ms）上报多击超时 |
| `multi_release_timeout_ticks` | 30 | 确认释放后再过 30 个 tick（约 300ms）结算点击序列 |

各阈值都从消抖确认的时刻开始计数：确认的那次扫描中 tick 计数被清零后又立即自增为 1，因此它等于“确认后已过的 tick 数”，阈值 N 就是确认后 N 个 tick 触发，`long_press_expired_ticks = 100` 即确认按下后再过 100 个 tick。若从电平跳变算起，实际时间还要再加上消抖本身，即多出 `press_debounce_ticks` 或 `release_debounce_ticks` 个 tick

### 5. 完整示例

```c
#include "simplekey.h"

#define KEY_NUM  2
#define KEY1_PIN 1
#define KEY2_PIN 2

static skey_t keys[KEY_NUM];
static skey_group_t group;
static skey_message_t queue_buffer[16];

/* 由事件回调写入，应用自行处理 */
static volatile uint8_t click_count;

extern int gpio_get_level(int pin); /* 应用自己的 GPIO 读取函数 */

/* 返回 0 表示按下；user_data 为按键的引脚号 */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 0 : 1;
}

/* 1 = 单击，2 = 双击，以此类推 */
static void key_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    (void)user_data;

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        click_count = press_count;
}

/* 由周期定时器中断调用，例如每 10ms 一次 */
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
        /* 在此处理 click_count */
    }
}
```
