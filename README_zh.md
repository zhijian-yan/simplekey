<h1 align="center">SimpleKey</h1>

<p align="center"><a href="README.md">English</a> | <a href="README_zh.md">简体中文</a></p>

<p align="center"><em>轻量级嵌入式按键扫描</em></p>

<p align="center">
<a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square"></a>
<img alt="Language" src="https://img.shields.io/badge/language-C99-blue.svg?style=flat-square">
<img alt="Dependencies" src="https://img.shields.io/badge/dependencies-none-brightgreen.svg?style=flat-square">
<img alt="Dynamic memory" src="https://img.shields.io/badge/dynamic_memory-none-brightgreen.svg?style=flat-square">
<img alt="Platform" src="https://img.shields.io/badge/platform-bare--metal_%7C_RTOS-lightgrey.svg?style=flat-square">
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
* 平台相关的队列顺序屏障由钩子层隔离

## 移植

移植 SimpleKey 时，在 `simplekey_port.h` 中实现下列两个钩子即可；该文件属于应用，升级库时保留自己的版本

### 平台钩子

| 钩子 | 作用 |
| ---- | ---- |
| `SKEY_ACQUIRE()` | 读取队列索引后、访问该索引发布的事件前的顺序屏障 |
| `SKEY_RELEASE()` | 写入发布事件的队列索引前的顺序屏障 |

### 默认实现

库内钩子默认全为空操作，适用于单核：事件队列为单生产者单消费者，队列索引与事件写入无需显式顺序保证。

### 示例：多核

队列为单生产者单消费者，只需要真正的顺序屏障：`volatile` 不保证顺序，一个核可能先看到索引、后看到事件。

```c
/* simplekey_port.h */
#include "cmsis_compiler.h"

#define SKEY_ACQUIRE() __DMB()
#define SKEY_RELEASE() __DMB()
```

缓存不一致时屏障不够：共享数据要放在非缓存内存，或显式维护缓存。

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

/* 返回非 0 表示按下；user_data 为按键的引脚号 */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 1 : 0;
}

/* 1 = 单击，2 = 双击，以此类推 */
static void key_event_cb(uint8_t events, uint8_t press_count, void *user_data) {
    (void)user_data;

    if (events & SKEY_EVENT_MULTI_RELEASE_TIMEOUT)
        click_count = press_count;
}
```

`key_read_cb()` 返回逻辑按下状态而非引脚原始电平：非 0 表示按下，0 表示释放。示例对低电平有效的按键做了取反；高电平有效的按键可直接返回电平值。

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

`skey_scan()` 的返回值为因事件队列已满而丢弃的事件数，立即模式下恒为 0；同一分组中，`skey_scan()` 与 `skey_dispatch()` 各自只接受一个执行上下文，不同分组之间相互独立。这里扫描在中断中执行、分发在主循环中执行；事件队列为单生产者单消费者，可直接使用库内默认实现

### 4. 选择时序参数

所有时长都以扫描周期的 tick 计数，若扫描周期为 10ms：

| 字段 | 示例值 | 含义 |
| ---- | ------ | ---- |
| `press_debounce_ticks` | 1 | 首次采到按下状态后约 10ms 确认按下 |
| `release_debounce_ticks` | 1 | 首次采到释放状态后约 10ms 确认释放 |
| `long_press_expired_ticks` | 100 | 确认按下后再过 100 个 tick（约 1s）上报长按 |
| `multi_press_timeout_ticks` | 30 | 确认按下后再过 30 个 tick（约 300ms）上报多击超时 |
| `multi_release_timeout_ticks` | 30 | 确认释放后再过 30 个 tick（约 300ms）结算点击序列 |

各阈值都从消抖确认的时刻开始计数：确认的那次扫描中 tick 计数被清零后又立即自增为 1，因此它等于“确认后已过的 tick 数”，阈值 N 就是确认后 N 个 tick 到达，`long_press_expired_ticks = 100` 即确认按下后再过 100 个 tick。若从状态跳变算起，实际时间还要再加上消抖本身，即多出 `press_debounce_ticks` 或 `release_debounce_ticks` 个 tick

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

/* 返回非 0 表示按下；user_data 为按键的引脚号 */
static uint8_t key_read_cb(void *user_data) {
    return (gpio_get_level((int)(intptr_t)user_data) == 0) ? 1 : 0;
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
