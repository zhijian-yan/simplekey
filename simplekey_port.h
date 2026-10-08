// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

/**
 * @file
 * @brief Platform hooks of simplekey.
 *
 * Provides the critical section and the queue ordering barriers that simplekey
 * needs from the target.
 *
 * This file belongs to the application rather than to the library: adapt it to
 * the target and keep it when simplekey is updated. To keep it outside the
 * library tree, define SKEY_PORT_HEADER to its name instead of editing the
 * shipped file.
 *
 * The defaults below are all no-ops, which is correct only while skey_scan()
 * and skey_dispatch() are called from a single execution context each.
 */

#ifndef SIMPLEKEY_PORT_H
#define SIMPLEKEY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter the critical section.
 *
 * skey_scan() calls this around the state update of one key, that is the
 * signal-layer and gesture-layer state machines and the fields they maintain,
 * so that the scanning context always sees a consistent key state.
 *
 * The critical section covers that update only: pushing the event into the
 * group's queue and invoking the event callback happen after skey_unlock() and
 * rely on the single-producer single-consumer model of the queue instead.
 *
 * @return Opaque state describing the situation before the critical section was
 *         entered; it is passed back to skey_unlock(), which must restore it
 *         exactly.
 *
 * @note The default implementation does nothing, which is correct only while
 *       skey_scan() and skey_dispatch() each run in a single execution context.
 * @note Keep the implementation stateless: a static inline definition is
 *       private to each translation unit, so a function-local static variable
 *       would exist once per translation unit instead of once per program.
 * @note If skey_scan() may run in an interrupt handler, the lock must disable
 *       interrupts, or use the ISR-safe entry point of the RTOS critical
 *       section. A plain spinlock that does not mask local interrupts deadlocks
 *       as soon as an ISR preempts the context holding it.
 * @note If skey_scan() may run in RTOS tasks, use the RTOS critical section or
 *       a mutex: disabling interrupts alone does not cover a task switch inside
 *       the critical section.
 */
static inline int skey_lock(void) {
    return 0;
}

/**
 * @brief Leave the critical section entered by skey_lock().
 *
 * @param skey_lock_state Value returned by the matching skey_lock() call.
 *
 * @note Nested lock/unlock pairs must restore the state of the outermost pair
 *       correctly, so save and restore the interrupt mask instead of
 *       unconditionally enabling interrupts.
 */
static inline void skey_unlock(int skey_lock_state) {
    (void)skey_lock_state;
}

/**
 * @brief Ordering barrier between a queued event and its index.
 *
 * Called after a queue index has been loaded and before the data that index
 * protects is touched.
 *
 * @note The default is a no-op, which is enough on a single core. A multi-core
 *       target needs a real acquire barrier here, for example the CMSIS
 *       __DMB(): volatile alone provides no ordering, so a core could otherwise
 *       observe an index before the event it publishes.
 */
#define SKEY_ACQUIRE() ((void)0)

/**
 * @brief Ordering barrier taken just before a queue index is stored.
 *
 * Called right before the index that publishes a queued event is written, so
 * that the event itself is visible first.
 *
 * @note The default is a no-op, which is enough on a single core; a multi-core
 *       target needs a real release barrier here. Keep it consistent with
 *       SKEY_ACQUIRE().
 */
#define SKEY_RELEASE() ((void)0)

#ifdef __cplusplus
}
#endif

#endif
