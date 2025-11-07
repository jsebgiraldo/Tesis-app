/* Custom OpenThread CLI Commands Header */

#ifndef OT_CUSTOM_COMMANDS_H
#define OT_CUSTOM_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize custom OpenThread CLI commands
 * 
 * Registers the following commands:
 * - discover <service>: Browse for specific service types (coap, coaps, lwm2m, http, https)
 * - findall: Browse for all common service types
 */
void ot_custom_commands_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OT_CUSTOM_COMMANDS_H */
