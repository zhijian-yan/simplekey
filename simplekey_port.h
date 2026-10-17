// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

/**
 * @file
 * @brief Platform hooks of SimpleKey: the critical section and queue ordering
 * barriers.
 *
 * This file belongs to the application: adapt it to the target and keep it
 * across library updates. To keep it outside the library tree, define
 * SKEY_PORT_HEADER to its name.
 */

#ifndef SIMPLEKEY_PORT_H
#define SIMPLEKEY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter the critical section.
 *
 * skey_scan() calls this around one key's state update (the signal and gesture
 * layers and their fields) so the scanning context sees a consistent state.
 * Only that update is covered: queueing the event and calling the event
 * callback happen after skey_unlock(), relying on the queue's single-producer
 * single-consumer model.
 *
 * @return Opaque pre-entry state, passed back to skey_unlock(), which must
 *         restore it exactly.
 *
 * @note The default no-op is correct only while skey_scan() and skey_dispatch()
 *       each run in one execution context.
 * @note Keep the hooks stateless: a static inline definition is private per
 *       translation unit, so a function-local static would be one per TU, not
 *       one per program.
 * @note If skey_scan() may run in an ISR, disable interrupts or use the
 *       ISR-safe RTOS critical section; a mutex is not ISR-safe, and a spinlock
 *       that does not mask local interrupts deadlocks when an ISR preempts its
 *       holder. For task-only use, an RTOS critical section or mutex works.
 */
static inline int skey_lock(void) {
    return 0;
}

/**
 * @brief Leave the critical section entered by skey_lock().
 *
 * @param skey_lock_state Value returned by the matching skey_lock() call.
 *
 * @note If nesting is supported, restore the outermost state; save and restore
 *       the interrupt mask instead of unconditionally enabling interrupts.
 */
static inline void skey_unlock(int skey_lock_state) {
    (void)skey_lock_state;
}

/**
 * @brief Ordering barrier between a queued event and its index.
 *
 * Called after a queue index load and before touching the data it protects.
 *
 * @note The default no-op is enough on a single core. A multi-core target needs
 *       a real acquire barrier (e.g. CMSIS __DMB()): volatile alone provides no
 *       ordering, so a core could observe an index before the event.
 */
#define SKEY_ACQUIRE() ((void)0)

/**
 * @brief Ordering barrier before a queue index is stored.
 *
 * Called before the index that publishes a queued event is written, so the
 * event is visible first.
 *
 * @note The default no-op is enough on a single core; a multi-core target needs
 *       a real release barrier. Keep it consistent with SKEY_ACQUIRE().
 */
#define SKEY_RELEASE() ((void)0)

#ifdef __cplusplus
}
#endif

#endif
