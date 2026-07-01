#ifndef MODULE_FLUIDICS_H
#define MODULE_FLUIDICS_H

#include "main.h"

/* bool là built-in trong C++; với C thuần cần include stdbool.h */
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ─── Khai báo biến external để file main có thể đọc được số giọt ────────────
extern volatile uint8_t count_drop_1;
extern volatile uint8_t count_drop_2;

// ─── Khai báo các hàm điều khiển bơm ────────────────────────────────────────
void Pump_Water_In(bool state);
void Pump_Water_Out(bool state);
void Pump_Reagent_1(bool state);   // Bật/Tắt cờ kích hoạt bơm nhu động LO_1
void Pump_Reagent_2(bool state);   // Bật/Tắt cờ kích hoạt bơm nhu động LO_2
void Reset_Drop_Counters(void);

// ─── Hàm gọi từ HAL_TIM_PeriodElapsedCallback (ISR TIM6, mỗi 1ms) ──────────
// Phát xung software PWM cho bơm nhu động và bơm màng
void Pump_System_TIM6_Callback(void);

#ifdef __cplusplus
}
#endif

#endif