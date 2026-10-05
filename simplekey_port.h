// SPDX-License-Identifier: MIT
// Copyright (c) 2025-2026 Zhijian Yan

#ifndef SIMPLEKEY_PORT_H
#define SIMPLEKEY_PORT_H

static inline int skey_lock(void) {
    return 0;
}

static inline void skey_unlock(int skey_lock_state) {
    (void)skey_lock_state;
}

#define SKEY_ACQUIRE() ((void)0)
#define SKEY_RELEASE() ((void)0)

#endif
