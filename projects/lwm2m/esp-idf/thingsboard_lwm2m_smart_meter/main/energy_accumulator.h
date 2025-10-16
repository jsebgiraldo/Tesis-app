#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double kwh_import;
    double kwh_export;
    double last_persist_time_s; // last persistence timestamp (monotonic seconds)
} EnergyAccumulator;

void energy_acc_init(EnergyAccumulator *acc);
void energy_acc_add(EnergyAccumulator *acc, double power_kw, double dt_s, double now_s);
// Force flush persistence regardless of interval
void energy_acc_flush(EnergyAccumulator *acc, double now_s);

#ifdef __cplusplus
}
#endif
