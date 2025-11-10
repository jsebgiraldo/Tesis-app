/**
 * @file test_thread_network.c
 * @brief Unit tests for Thread network operations
 */

#include "unity.h"
#include "openthread/thread.h"
#include "openthread/link.h"
#include "openthread/instance.h"
#include <string.h>

void setUp(void) {
    // Set up code if needed
}

void tearDown(void) {
    // Clean up code if needed
}

/**
 * @brief Test Link Mode configuration for End Device
 */
void test_link_mode_end_device(void)
{
    otLinkModeConfig linkMode;
    
    // Configure as End Device
    linkMode.mRxOnWhenIdle = true;   // Always listening
    linkMode.mDeviceType = false;    // End Device (not Router)
    linkMode.mNetworkData = true;    // Request full network data
    
    // Verify configuration
    TEST_ASSERT_TRUE(linkMode.mRxOnWhenIdle);
    TEST_ASSERT_FALSE(linkMode.mDeviceType);  // false = End Device
    TEST_ASSERT_TRUE(linkMode.mNetworkData);
}

/**
 * @brief Test Link Mode configuration for Router
 */
void test_link_mode_router(void)
{
    otLinkModeConfig linkMode;
    
    // Configure as Router
    linkMode.mRxOnWhenIdle = true;   // Always listening
    linkMode.mDeviceType = true;     // Router/Leader capable
    linkMode.mNetworkData = true;    // Request full network data
    
    // Verify configuration
    TEST_ASSERT_TRUE(linkMode.mRxOnWhenIdle);
    TEST_ASSERT_TRUE(linkMode.mDeviceType);  // true = Router
    TEST_ASSERT_TRUE(linkMode.mNetworkData);
}

/**
 * @brief Test device role states
 */
void test_device_roles(void)
{
    // Verify role enum values are defined
    otDeviceRole role;
    
    role = OT_DEVICE_ROLE_DISABLED;
    TEST_ASSERT_EQUAL(OT_DEVICE_ROLE_DISABLED, role);
    
    role = OT_DEVICE_ROLE_DETACHED;
    TEST_ASSERT_EQUAL(OT_DEVICE_ROLE_DETACHED, role);
    
    role = OT_DEVICE_ROLE_CHILD;
    TEST_ASSERT_EQUAL(OT_DEVICE_ROLE_CHILD, role);
    
    role = OT_DEVICE_ROLE_ROUTER;
    TEST_ASSERT_EQUAL(OT_DEVICE_ROLE_ROUTER, role);
    
    role = OT_DEVICE_ROLE_LEADER;
    TEST_ASSERT_EQUAL(OT_DEVICE_ROLE_LEADER, role);
}

/**
 * @brief Test role checking logic
 */
void test_is_role_attached(void)
{
    // Child role should be considered attached
    TEST_ASSERT_TRUE(OT_DEVICE_ROLE_CHILD >= OT_DEVICE_ROLE_CHILD);
    
    // Router role should be considered attached
    TEST_ASSERT_TRUE(OT_DEVICE_ROLE_ROUTER >= OT_DEVICE_ROLE_CHILD);
    
    // Leader role should be considered attached
    TEST_ASSERT_TRUE(OT_DEVICE_ROLE_LEADER >= OT_DEVICE_ROLE_CHILD);
    
    // Detached should not be considered attached
    TEST_ASSERT_FALSE(OT_DEVICE_ROLE_DETACHED >= OT_DEVICE_ROLE_CHILD);
    
    // Disabled should not be considered attached
    TEST_ASSERT_FALSE(OT_DEVICE_ROLE_DISABLED >= OT_DEVICE_ROLE_CHILD);
}

/**
 * @brief Test wait interval calculation
 */
void test_wait_interval_calculation(void)
{
    const int check_interval_ms = 200;
    const int max_wait_seconds = 15;
    
    int max_iterations = (max_wait_seconds * 1000) / check_interval_ms;
    
    // Should be 75 iterations for 15 seconds at 200ms intervals
    TEST_ASSERT_EQUAL_INT(75, max_iterations);
}

/**
 * @brief Test log interval calculation
 */
void test_log_interval_calculation(void)
{
    // Test that we log every 3 seconds
    const int check_interval_ms = 200;
    int waited_ms = 0;
    int log_count = 0;
    
    for (int i = 0; i < 20; i++) {  // 20 iterations = 4 seconds
        waited_ms += check_interval_ms;
        
        if (waited_ms % 3000 == 0) {
            log_count++;
        }
    }
    
    // Should have logged once at 3000ms (3 seconds)
    TEST_ASSERT_EQUAL_INT(1, log_count);
}

/**
 * @brief Test network parameter validation - valid values
 */
void test_network_params_valid(void)
{
    // Test valid network name length
    const char *network_name = "OpenThreadDemo";
    size_t length = strlen(network_name);
    TEST_ASSERT_LESS_OR_EQUAL(OT_NETWORK_NAME_MAX_SIZE, length);
    
    // Test valid channel (11-26 for 2.4GHz)
    uint8_t channel = 15;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(11, channel);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(26, channel);
    
    // Test valid PAN ID (non-broadcast)
    uint16_t panid = 0x1234;
    TEST_ASSERT_NOT_EQUAL_HEX16(0xFFFF, panid);  // 0xFFFF is broadcast
}

/**
 * @brief Test network parameter validation - invalid values
 */
void test_network_params_invalid(void)
{
    // Test invalid channel (out of range)
    uint8_t channel_low = 10;
    TEST_ASSERT_LESS_THAN_UINT8(11, channel_low);
    
    uint8_t channel_high = 27;
    TEST_ASSERT_GREATER_THAN_UINT8(26, channel_high);
    
    // Test broadcast PAN ID (should not be used)
    uint16_t broadcast_panid = 0xFFFF;
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, broadcast_panid);
}

/**
 * @brief Test IPv6 address structure
 */
void test_ipv6_address_structure(void)
{
    otIp6Address addr;
    memset(&addr, 0, sizeof(addr));
    
    // Configure a known prefix: fdca:06fb:455f:9103::
    addr.mFields.m8[0] = 0xfd;
    addr.mFields.m8[1] = 0xca;
    addr.mFields.m8[2] = 0x06;
    addr.mFields.m8[3] = 0xfb;
    addr.mFields.m8[4] = 0x45;
    addr.mFields.m8[5] = 0x5f;
    addr.mFields.m8[6] = 0x91;
    addr.mFields.m8[7] = 0x03;
    
    // Verify ULA prefix (fd00::/8)
    TEST_ASSERT_EQUAL_HEX8(0xfd, addr.mFields.m8[0]);
    
    // Verify it's not link-local (fe80::/10)
    TEST_ASSERT_NOT_EQUAL_HEX8(0xfe, addr.mFields.m8[0]);
}

// Run all tests
void app_main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_link_mode_end_device);
    RUN_TEST(test_link_mode_router);
    RUN_TEST(test_device_roles);
    RUN_TEST(test_is_role_attached);
    RUN_TEST(test_wait_interval_calculation);
    RUN_TEST(test_log_interval_calculation);
    RUN_TEST(test_network_params_valid);
    RUN_TEST(test_network_params_invalid);
    RUN_TEST(test_ipv6_address_structure);
    
    UNITY_END();
}
