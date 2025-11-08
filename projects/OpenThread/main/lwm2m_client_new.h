/* LwM2M Client for OpenThread using Anjay */

#pragma once

#include <anjay/anjay.h>
#include "ot_auto_discovery.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el cliente LwM2M con Anjay
 * 
 * Esta función debe llamarse después de que el dispositivo Thread
 * obtenga una dirección IPv6 válida.
 * 
 * @return ESP_OK en éxito
 */
esp_err_t lwm2m_client_init(void);

/**
 * @brief Inicia el cliente LwM2M en una tarea independiente
 * 
 * Crea una tarea FreeRTOS que ejecuta el event loop de Anjay.
 * 
 * @return ESP_OK en éxito
 */
esp_err_t lwm2m_client_start(void);

/**
 * @brief Detiene y libera recursos del cliente LwM2M
 * 
 * @return ESP_OK en éxito
 */
esp_err_t lwm2m_client_stop(void);

/**
 * @brief Obtiene el estado actual del cliente LwM2M
 * 
 * @return true si el cliente está registrado en el servidor, false en caso contrario
 */
bool lwm2m_client_is_registered(void);

/**
 * @brief Obtiene estado como string
 * 
 * @return Nombre del estado actual
 */
const char* lwm2m_client_get_state_str(void);

/**
 * @brief Configura automáticamente el cliente con el servidor descubierto
 * 
 * Utiliza información del auto-discovery (dirección IPv6 y puerto) para
 * configurar Security y Server objects de Anjay.
 * 
 * @param service Servicio LwM2M descubierto
 * @return ESP_OK en éxito
 */
esp_err_t lwm2m_client_auto_configure(const discovered_service_t *service);

/**
 * @brief Actualiza el valor de temperatura simulada
 * 
 * Actualiza el IPSO Temperature Object (3303) y notifica al servidor
 * si hay una observación activa.
 * 
 * @param temperature Valor de temperatura en grados Celsius
 * @return ESP_OK en éxito
 */
esp_err_t lwm2m_client_update_temperature(float temperature);

#ifdef __cplusplus
}
#endif
