#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize OpenThread platform and start joiner commissioning if configured.
// Returns true if joiner process was started (or dataset already active), false on error.
bool thread_prov_start(void);

// Poll to determine if Thread is attached (has an IPv6 mesh-local EID and link up).
bool thread_prov_is_attached(void);

// Retrieve a preferred IPv6 address (mesh-local or global) as string.
// Returns false if not yet available.
bool thread_prov_get_ip(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
