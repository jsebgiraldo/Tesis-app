#include "temp_object.h"
#include <math.h>
#include <stdbool.h>
#include <anjay/anjay.h>

// Simple mocked temperature source
static float read_temperature(void) {
	// NOTE: replace with real sensor driver when available
	extern unsigned long xTaskGetTickCount(void);
	float base = 25.0f;
	float delta = 2.0f * sinf((float)(xTaskGetTickCount() % 10000) / 1000.0f);
	return base + delta;
}

// Track min/max measured values
static bool g_have_value = false;
static float g_min_measured = 0.0f;
static float g_max_measured = 0.0f;

static int temp_list_instances(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
							   anjay_dm_list_ctx_t *ctx) {
	(void) anjay; (void) def;
	anjay_dm_emit(ctx, 0);
	return 0;
}

static int temp_list_resources(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
							   anjay_iid_t iid, anjay_dm_resource_list_ctx_t *ctx) {
	(void) anjay; (void) def; (void) iid;
	// 5700: Sensor Value (float, R)
	anjay_dm_emit_res(ctx, 5700, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
	// 5701: Units (string, R)
	anjay_dm_emit_res(ctx, 5701, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
	// 5601/5602: Min/Max Measured Value (float, R)
	anjay_dm_emit_res(ctx, 5601, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
	anjay_dm_emit_res(ctx, 5602, ANJAY_DM_RES_R, ANJAY_DM_RES_PRESENT);
	// 5605: Reset Min and Max Measured Values (Exec -> CoAP POST)
	anjay_dm_emit_res(ctx, 5605, ANJAY_DM_RES_E, ANJAY_DM_RES_PRESENT);
	return 0;
}

static int temp_read(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
					 anjay_iid_t iid, anjay_rid_t rid, anjay_riid_t riid,
					 anjay_output_ctx_t *out_ctx) {
	(void) anjay; (void) def; (void) iid; (void) riid;
	switch (rid) {
	case 5700: {
		float t = read_temperature();
		if (!g_have_value) {
			g_have_value = true;
			g_min_measured = t;
			g_max_measured = t;
		} else {
			if (t < g_min_measured) { g_min_measured = t; }
			if (t > g_max_measured) { g_max_measured = t; }
		}
		return anjay_ret_float(out_ctx, t);
	}
	case 5701:
		return anjay_ret_string(out_ctx, "Cel");
	case 5601:
		return anjay_ret_float(out_ctx, g_have_value ? g_min_measured : 0.0f);
	case 5602:
		return anjay_ret_float(out_ctx, g_have_value ? g_max_measured : 0.0f);
	default:
		return ANJAY_ERR_METHOD_NOT_ALLOWED;
	}
}

static int temp_execute(anjay_t *anjay, const anjay_dm_object_def_t *const *def,
						anjay_iid_t iid, anjay_rid_t rid, anjay_execute_ctx_t *arg_ctx) {
	(void) def; (void) iid; (void) arg_ctx;
	switch (rid) {
	case 5605: { // Reset Min and Max Measured Values
		float t = read_temperature();
		g_have_value = true;
		g_min_measured = t;
		g_max_measured = t;
		// Notify server that 5601 and 5602 changed
		(void) anjay_notify_changed(anjay, 3303, 0, 5601);
		(void) anjay_notify_changed(anjay, 3303, 0, 5602);
		return 0;
	}
	default:
		return ANJAY_ERR_METHOD_NOT_ALLOWED;
	}
}

static const anjay_dm_object_def_t OBJ3303 = {
	.oid = 3303,
	.handlers = {
		.list_instances = temp_list_instances,
		.list_resources = temp_list_resources,
		.resource_read = temp_read,
	.resource_execute = temp_execute,
	}
};

static const anjay_dm_object_def_t *const OBJ3303_DEF = &OBJ3303;

const anjay_dm_object_def_t *const *temp_object_def(void) {
	return &OBJ3303_DEF;
}
