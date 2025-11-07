/* Custom OpenThread CLI Commands for Service Discovery */

#include "esp_openthread.h"
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "openthread/dns_client.h"
#include "esp_log.h"
#include "ot_auto_discovery.h"
#include <string.h>
#include <stdio.h>

#define TAG "ot_custom_cmd"

// Helper function to execute DNS browse command
static void execute_dns_browse(const char *service_type)
{
    char command[128];
    snprintf(command, sizeof(command), "dns browse %s.default.service.arpa.", service_type);
    
    ESP_LOGI(TAG, "Executing: %s", command);
    
    // Execute the command through OpenThread CLI (void return in v5.3.1)
    otCliInputLine(command);
}

// Handler for "discover" command
static otError discover_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength == 0) {
        otCliOutputFormat("Usage: discover <service>\r\n");
        otCliOutputFormat("Services: coap, coaps, lwm2m, http, https\r\n");
        otCliOutputFormat("Example: discover coap\r\n");
        return OT_ERROR_INVALID_ARGS;
    }

    const char *service = aArgs[0];
    
    if (strcmp(service, "coap") == 0) {
        execute_dns_browse("_coap._udp");
    }
    else if (strcmp(service, "coaps") == 0) {
        execute_dns_browse("_coaps._udp");
    }
    else if (strcmp(service, "lwm2m") == 0) {
        execute_dns_browse("_lwm2m._udp");
    }
    else if (strcmp(service, "http") == 0) {
        execute_dns_browse("_http._tcp");
    }
    else if (strcmp(service, "https") == 0) {
        execute_dns_browse("_https._tcp");
    }
    else {
        otCliOutputFormat("Unknown service: %s\r\n", service);
        otCliOutputFormat("Available: coap, coaps, lwm2m, http, https\r\n");
        return OT_ERROR_INVALID_ARGS;
    }
    
    return OT_ERROR_NONE;
}

// Handler for "findall" command (discover all services)
static otError findall_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otCliOutputFormat("Searching for all services...\r\n");
    
    execute_dns_browse("_coap._udp");
    execute_dns_browse("_coaps._udp");
    execute_dns_browse("_lwm2m._udp");
    execute_dns_browse("_http._tcp");
    
    return OT_ERROR_NONE;
}

// Handler for "autostart" command
static otError autostart_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otCliOutputFormat("Starting auto-discovery process...\r\n");
    
    esp_err_t result = ot_auto_discovery_start();
    if (result == ESP_OK) {
        otCliOutputFormat("Auto-discovery started successfully\r\n");
    } else {
        otCliOutputFormat("Failed to start auto-discovery: %s\r\n", esp_err_to_name(result));
    }
    
    return OT_ERROR_NONE;
}

// Handler for "autostatus" command
static otError autostatus_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otCliOutputFormat("=== AUTO-DISCOVERY STATUS ===\r\n");
    otCliOutputFormat("State: %s\r\n", ot_auto_discovery_get_state());
    otCliOutputFormat("Connected: %s\r\n", ot_auto_discovery_is_connected() ? "Yes" : "No");
    
    // Show discovered networks
    thread_network_t networks[10];
    int network_count = ot_auto_discovery_get_networks(networks, 10);
    otCliOutputFormat("Networks found: %d\r\n", network_count);
    
    for (int i = 0; i < network_count; i++) {
        otCliOutputFormat("  %d. %s (PAN: 0x%04X, Ch: %d, RSSI: %d, Joinable: %s)\r\n",
                         i+1, networks[i].network_name, networks[i].panid,
                         networks[i].channel, networks[i].rssi,
                         networks[i].joinable ? "Yes" : "No");
    }
    
    // Show discovered services
    discovered_service_t services[10];
    int service_count = ot_auto_discovery_get_services(services, 10);
    otCliOutputFormat("Services found: %d\r\n", service_count);
    
    for (int i = 0; i < service_count; i++) {
        otCliOutputFormat("  %d. %s (%s) at %s:%d - Ping: %s",
                         i+1, services[i].service_name, services[i].service_type,
                         services[i].ipv6_addr, services[i].port,
                         services[i].ping_success ? "OK" : "FAIL");
        if (services[i].ping_success) {
            otCliOutputFormat(" (%ums)", services[i].ping_time_ms);
        }
        otCliOutputFormat("\r\n");
    }
    
    return OT_ERROR_NONE;
}

// Handler for "setnetkey" command
static otError setnetkey_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    if (aArgsLength != 1) {
        otCliOutputFormat("Usage: setnetkey <network_key_hex>\r\n");
        otCliOutputFormat("Example: setnetkey 00112233445566778899aabbccddeeff\r\n");
        otCliOutputFormat("Get key from Border Router with: sudo ot-ctl networkkey\r\n");
        return OT_ERROR_INVALID_ARGS;
    }

    const char *key_str = aArgs[0];
    
    // Validate hex string length (32 hex chars = 16 bytes)
    if (strlen(key_str) != 32) {
        otCliOutputFormat("Error: Network key must be 32 hex characters (16 bytes)\r\n");
        return OT_ERROR_INVALID_ARGS;
    }

    // Validate hex characters
    for (int i = 0; i < 32; i++) {
        char c = key_str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            otCliOutputFormat("Error: Invalid hex character '%c' at position %d\r\n", c, i);
            return OT_ERROR_INVALID_ARGS;
        }
    }

    // Convert hex string to bytes (for validation)
    for (int i = 0; i < 16; i++) {
        char byte_str[3] = {key_str[i*2], key_str[i*2+1], '\0'};
        // Just validate, don't store (we use the string directly)
        (void)strtol(byte_str, NULL, 16);
    }

    // Apply the network key using OpenThread CLI
    char dataset_cmd[64];
    snprintf(dataset_cmd, sizeof(dataset_cmd), "dataset networkkey %s", key_str);
    
    otCliOutputFormat("Setting network key: %s\r\n", key_str);
    otCliInputLine(dataset_cmd);
    otCliInputLine("dataset commit active");
    
    otCliOutputFormat("Network key updated! Restart auto-join with 'autostart'\r\n");
    
    return OT_ERROR_NONE;
}

// Handler for "joinbr" command - Quick join Border Router
static otError joinbr_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    otCliOutputFormat("=== QUICK BORDER ROUTER JOIN ===\r\n");
    otCliOutputFormat("Configuring Border Router credentials...\r\n");
    
    // Apply Border Router configuration
    otCliInputLine("dataset networkname OpenThreadDemo");
    otCliInputLine("dataset panid 0x1234");
    otCliInputLine("dataset channel 15");
    otCliInputLine("dataset extpanid 1111111122222222");
    
    if (aArgsLength == 1) {
        // Network key provided
        char dataset_cmd[64];
        snprintf(dataset_cmd, sizeof(dataset_cmd), "dataset networkkey %s", aArgs[0]);
        otCliInputLine(dataset_cmd);
        otCliOutputFormat("Using provided network key: %s\r\n", aArgs[0]);
    } else {
        otCliOutputFormat("WARNING: Using default network key!\r\n");
        otCliOutputFormat("Get real key with: sudo ot-ctl networkkey\r\n");
        otCliInputLine("dataset networkkey 00112233445566778899aabbccddeeff");
    }
    
    otCliInputLine("dataset commit active");
    otCliInputLine("ifconfig up");
    otCliInputLine("thread start");
    
    otCliOutputFormat("Commands sent! Check connection with 'state'\r\n");
    otCliOutputFormat("Usage: joinbr [network_key]\r\n");
    
    return OT_ERROR_NONE;
}

// Handler for "bestlwm2m" command
static otError bestlwm2m_command_handler(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    discovered_service_t best_service;
    
    if (ot_auto_discovery_get_best_lwm2m_service(&best_service)) {
        otCliOutputFormat("=== BEST LwM2M SERVICE ===\r\n");
        otCliOutputFormat("Name: %s\r\n", best_service.service_name);
        otCliOutputFormat("Type: %s\r\n", best_service.service_type);
        otCliOutputFormat("Host: %s\r\n", best_service.hostname);
        otCliOutputFormat("IPv6: %s\r\n", best_service.ipv6_addr);
        otCliOutputFormat("Port: %d\r\n", best_service.port);
        otCliOutputFormat("Ping: %s", best_service.ping_success ? "OK" : "FAIL");
        if (best_service.ping_success) {
            otCliOutputFormat(" (%ums)", best_service.ping_time_ms);
        }
        otCliOutputFormat("\r\n");
        otCliOutputFormat("Use this for LwM2M connection!\r\n");
    } else {
        otCliOutputFormat("No suitable LwM2M service found\r\n");
        otCliOutputFormat("Make sure to run 'autostart' first\r\n");
    }
    
    return OT_ERROR_NONE;
}

// Command table
static const otCliCommand kCommands[] = {
    {"discover", discover_command_handler},
    {"findall", findall_command_handler},
    {"autostart", autostart_command_handler},
    {"autostatus", autostatus_command_handler},
    {"bestlwm2m", bestlwm2m_command_handler},
    {"setnetkey", setnetkey_command_handler},
    {"joinbr", joinbr_command_handler},
};

// Initialize custom commands
void ot_custom_commands_init(void)
{
    otInstance *instance = esp_openthread_get_instance();
    otError error = otCliSetUserCommands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), instance);
    
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to register custom commands: %d", error);
    } else {
        ESP_LOGI(TAG, "Custom commands registered successfully");
        ESP_LOGI(TAG, "Available commands:");
        ESP_LOGI(TAG, "  joinbr [netkey] - Quick join Border Router");
        ESP_LOGI(TAG, "  setnetkey <hex> - Set network key");
        ESP_LOGI(TAG, "  autostart      - Start auto-discovery");
        ESP_LOGI(TAG, "  autostatus     - Check discovery status");
        ESP_LOGI(TAG, "  discover <svc> - Find specific service");
        ESP_LOGI(TAG, "  findall        - Find all services");
        ESP_LOGI(TAG, "  bestlwm2m      - Get best LwM2M service");
    }
}
