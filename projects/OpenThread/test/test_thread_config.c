/**
 * @file test_thread_config.c
 * @brief Unit tests for Thread network configuration
 */

#include "unity.h"
#include "openthread/dataset.h"
#include "openthread/instance.h"
#include <string.h>

// Test configuration values
#define TEST_NETWORK_NAME "OpenThreadDemo"
#define TEST_PANID 0x1234
#define TEST_CHANNEL 15
#define TEST_EXT_PANID 0x1111111122222222ULL

void setUp(void) {
    // Set up code if needed
}

void tearDown(void) {
    // Clean up code if needed
}

/**
 * @brief Test dataset structure initialization
 */
void test_dataset_initialization(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    // Verify all components are initially false
    TEST_ASSERT_FALSE(dataset.mComponents.mIsNetworkNamePresent);
    TEST_ASSERT_FALSE(dataset.mComponents.mIsPanIdPresent);
    TEST_ASSERT_FALSE(dataset.mComponents.mIsChannelPresent);
    TEST_ASSERT_FALSE(dataset.mComponents.mIsExtendedPanIdPresent);
    TEST_ASSERT_FALSE(dataset.mComponents.mIsNetworkKeyPresent);
}

/**
 * @brief Test network name configuration
 */
void test_network_name_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    const char *network_name = TEST_NETWORK_NAME;
    size_t length = strlen(network_name);
    
    memcpy(dataset.mNetworkName.m8, network_name, length);
    dataset.mComponents.mIsNetworkNamePresent = true;
    
    // Verify network name was set correctly
    TEST_ASSERT_TRUE(dataset.mComponents.mIsNetworkNamePresent);
    TEST_ASSERT_EQUAL_STRING_LEN(network_name, (char*)dataset.mNetworkName.m8, length);
}

/**
 * @brief Test PAN ID configuration
 */
void test_panid_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    dataset.mPanId = TEST_PANID;
    dataset.mComponents.mIsPanIdPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsPanIdPresent);
    TEST_ASSERT_EQUAL_HEX16(TEST_PANID, dataset.mPanId);
}

/**
 * @brief Test channel configuration
 */
void test_channel_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    dataset.mChannel = TEST_CHANNEL;
    dataset.mComponents.mIsChannelPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsChannelPresent);
    TEST_ASSERT_EQUAL_UINT8(TEST_CHANNEL, dataset.mChannel);
}

/**
 * @brief Test Extended PAN ID configuration
 */
void test_ext_panid_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    uint64_t extPanId = TEST_EXT_PANID;
    for (int i = 0; i < 8; i++) {
        dataset.mExtendedPanId.m8[i] = (extPanId >> (56 - i * 8)) & 0xff;
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsExtendedPanIdPresent);
    
    // Verify bytes match
    uint64_t reconstructed = 0;
    for (int i = 0; i < 8; i++) {
        reconstructed |= ((uint64_t)dataset.mExtendedPanId.m8[i]) << (56 - i * 8);
    }
    TEST_ASSERT_EQUAL_HEX64(TEST_EXT_PANID, reconstructed);
}

/**
 * @brief Test network key configuration
 */
void test_network_key_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    uint8_t network_key[OT_NETWORK_KEY_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    
    memcpy(dataset.mNetworkKey.m8, network_key, OT_NETWORK_KEY_SIZE);
    dataset.mComponents.mIsNetworkKeyPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsNetworkKeyPresent);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(network_key, dataset.mNetworkKey.m8, OT_NETWORK_KEY_SIZE);
}

/**
 * @brief Test Mesh-Local Prefix configuration
 */
void test_mesh_local_prefix_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    // Configure fdca:06fb:455f:9103::/64
    otIp6Prefix meshLocalPrefix;
    memset(&meshLocalPrefix, 0, sizeof(meshLocalPrefix));
    meshLocalPrefix.mPrefix.mFields.m8[0] = 0xfd;
    meshLocalPrefix.mPrefix.mFields.m8[1] = 0xca;
    meshLocalPrefix.mPrefix.mFields.m8[2] = 0x06;
    meshLocalPrefix.mPrefix.mFields.m8[3] = 0xfb;
    meshLocalPrefix.mPrefix.mFields.m8[4] = 0x45;
    meshLocalPrefix.mPrefix.mFields.m8[5] = 0x5f;
    meshLocalPrefix.mPrefix.mFields.m8[6] = 0x91;
    meshLocalPrefix.mPrefix.mFields.m8[7] = 0x03;
    meshLocalPrefix.mLength = 64;
    
    memcpy(&dataset.mMeshLocalPrefix, &meshLocalPrefix.mPrefix, sizeof(otIp6Address));
    dataset.mComponents.mIsMeshLocalPrefixPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsMeshLocalPrefixPresent);
    TEST_ASSERT_EQUAL_HEX8(0xfd, dataset.mMeshLocalPrefix.mFields.m8[0]);
    TEST_ASSERT_EQUAL_HEX8(0xca, dataset.mMeshLocalPrefix.mFields.m8[1]);
    TEST_ASSERT_EQUAL_HEX8(0x06, dataset.mMeshLocalPrefix.mFields.m8[2]);
    TEST_ASSERT_EQUAL_HEX8(0xfb, dataset.mMeshLocalPrefix.mFields.m8[3]);
}

/**
 * @brief Test Channel Mask configuration
 */
void test_channel_mask_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    dataset.mChannelMask = 1 << TEST_CHANNEL;
    dataset.mComponents.mIsChannelMaskPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsChannelMaskPresent);
    TEST_ASSERT_EQUAL_UINT32(1 << TEST_CHANNEL, dataset.mChannelMask);
}

/**
 * @brief Test Security Policy configuration
 */
void test_security_policy_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    dataset.mSecurityPolicy.mRotationTime = 672;
    dataset.mSecurityPolicy.mObtainNetworkKeyEnabled = true;
    dataset.mSecurityPolicy.mNativeCommissioningEnabled = true;
    dataset.mSecurityPolicy.mRoutersEnabled = true;
    dataset.mSecurityPolicy.mExternalCommissioningEnabled = true;
    dataset.mComponents.mIsSecurityPolicyPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsSecurityPolicyPresent);
    TEST_ASSERT_EQUAL_UINT16(672, dataset.mSecurityPolicy.mRotationTime);
    TEST_ASSERT_TRUE(dataset.mSecurityPolicy.mObtainNetworkKeyEnabled);
    TEST_ASSERT_TRUE(dataset.mSecurityPolicy.mNativeCommissioningEnabled);
    TEST_ASSERT_TRUE(dataset.mSecurityPolicy.mRoutersEnabled);
    TEST_ASSERT_TRUE(dataset.mSecurityPolicy.mExternalCommissioningEnabled);
}

/**
 * @brief Test Active Timestamp configuration
 */
void test_active_timestamp_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    dataset.mActiveTimestamp.mSeconds = 1;
    dataset.mActiveTimestamp.mTicks = 0;
    dataset.mActiveTimestamp.mAuthoritative = false;
    dataset.mComponents.mIsActiveTimestampPresent = true;
    
    TEST_ASSERT_TRUE(dataset.mComponents.mIsActiveTimestampPresent);
    TEST_ASSERT_EQUAL_UINT64(1, dataset.mActiveTimestamp.mSeconds);
    TEST_ASSERT_EQUAL_UINT16(0, dataset.mActiveTimestamp.mTicks);
    TEST_ASSERT_FALSE(dataset.mActiveTimestamp.mAuthoritative);
}

/**
 * @brief Test complete dataset configuration
 */
void test_complete_dataset_configuration(void)
{
    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(dataset));
    
    // Configure all fields
    const char *network_name = TEST_NETWORK_NAME;
    size_t length = strlen(network_name);
    memcpy(dataset.mNetworkName.m8, network_name, length);
    dataset.mComponents.mIsNetworkNamePresent = true;
    
    dataset.mPanId = TEST_PANID;
    dataset.mComponents.mIsPanIdPresent = true;
    
    dataset.mChannel = TEST_CHANNEL;
    dataset.mComponents.mIsChannelPresent = true;
    
    uint64_t extPanId = TEST_EXT_PANID;
    for (int i = 0; i < 8; i++) {
        dataset.mExtendedPanId.m8[i] = (extPanId >> (56 - i * 8)) & 0xff;
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;
    
    // Verify all components are present
    TEST_ASSERT_TRUE(dataset.mComponents.mIsNetworkNamePresent);
    TEST_ASSERT_TRUE(dataset.mComponents.mIsPanIdPresent);
    TEST_ASSERT_TRUE(dataset.mComponents.mIsChannelPresent);
    TEST_ASSERT_TRUE(dataset.mComponents.mIsExtendedPanIdPresent);
}

// Run all tests
void app_main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_dataset_initialization);
    RUN_TEST(test_network_name_configuration);
    RUN_TEST(test_panid_configuration);
    RUN_TEST(test_channel_configuration);
    RUN_TEST(test_ext_panid_configuration);
    RUN_TEST(test_network_key_configuration);
    RUN_TEST(test_mesh_local_prefix_configuration);
    RUN_TEST(test_channel_mask_configuration);
    RUN_TEST(test_security_policy_configuration);
    RUN_TEST(test_active_timestamp_configuration);
    RUN_TEST(test_complete_dataset_configuration);
    
    UNITY_END();
}
