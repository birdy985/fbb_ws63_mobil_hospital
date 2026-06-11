/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */

/**
 * @file cxx_adapter.c
 * @brief Stub implementations for C++ runtime system function dependencies
 * @version 1.0
 * @date 2026-04-21
 *
 * libsupc++ requires some system functions that LiteOS doesn't provide:
 * - dl_iterate_phdr: Dynamic linker iterator (for exception handling)
 * - __tls_get_addr: TLS address getter (for exception handling)
 * - write: System call (for __cxa_pure_virtual debug output)
 *
 * This file provides stub implementations to satisfy the linker.
 */

#ifdef SUPPORT_CXX
#include <stdint.h>
#include <stddef.h>

// Stub for dl_iterate_phdr - required by libgcc exception handling
// Returns 0 to indicate no shared objects found
#ifdef __cplusplus
extern "C"
#endif
int dl_iterate_phdr(void *callback, void *data)
{
    (void)callback;
    (void)data;
    return 0;
}

// Stub for __tls_get_addr - required by libgcc TLS support
// Returns NULL as we don't use thread-local storage
#ifdef __cplusplus
extern "C"
#endif
void *__tls_get_addr(void *arg)
{
    (void)arg;
    return NULL;
}

// Stub for write - required by __cxa_pure_virtual for debug output
// Returns -1 to indicate write failed (no debug output)
#ifdef __cplusplus
extern "C"
#endif
int write(int fd, const void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

#endif /* SUPPORT_CXX */