// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

/**
 * @file
 * @brief Platform hooks of SimpleKey: the queue ordering barriers.
 *
 * This file belongs to the application: adapt it to the target and keep it
 * across library updates. To keep it outside the library tree, define
 * SKEY_PORT_HEADER to its name.
 */

#ifndef SIMPLEKEY_PORT_H
#define SIMPLEKEY_PORT_H

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

#endif
