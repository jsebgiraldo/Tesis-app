/**
 * Custom mbedTLS configuration for Anjay LwM2M
 * This file ensures PSK functions are available
 */

#ifndef MBEDTLS_CONFIG_ESP_H
#define MBEDTLS_CONFIG_ESP_H

/* Include the default ESP-IDF mbedTLS config */
#include_next "mbedtls/mbedtls_config.h"

/* Force enable PSK support */
#define MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#define MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED

/* Ensure SSL functions are available */
#ifndef MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_SRV_C
#endif

#ifndef MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_CLI_C
#endif

#endif /* MBEDTLS_CONFIG_ESP_H */
