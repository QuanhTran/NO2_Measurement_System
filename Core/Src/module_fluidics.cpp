#include "module_fluidics.h"

// ─── Biến đếm giọt (dùng bởi ngắt EXTI) ────────────────────────────────────
volatile uint8_t count_drop_1 = 0;
volatile uint8_t count_drop_2 = 0;

// ─── Mức logic kích Mosfet (Active-High) ────────────────────────────────────
#define RELAY_ON  GPIO_PIN_SET
#define RELAY_OFF GPIO_PIN_RESET

// ─── Cờ kích hoạt bơm nhu động (set bởi app, đọc bởi ISR TIM6) ─────────────
static volatile uint8_t lo1_enabled = 0;
static volatile uint8_t lo2_enabled = 0;

// ─── Cờ kích hoạt bơm màng ────────────────────────────────────────────────
static volatile uint8_t bom_cap_enabled = 0;
static volatile uint8_t bom_xa_enabled = 0;

// ─── Bộ đếm tick (ms) nội bộ cho từng bơm ──────────────────────────────────
// Bơm nhu động (Chu trình: 0..4 → ON (5ms), 5..404 → OFF (400ms))
static volatile uint16_t lo1_tick = 0;
static volatile uint16_t lo2_tick = 0;

#define PUMP_PERIOD_TICKS  405U  // 5ms ON + 400ms OFF
#define PUMP_ON_TICKS        5U  // Số tick mức HIGH

// Bơm màng (PWM 100Hz: 10ms chu kỳ, 5ms ON = 50% duty cycle)
static volatile uint16_t bom_cap_tick = 0;
static volatile uint16_t bom_xa_tick = 0;

#define DIAPHRAGM_PERIOD_TICKS  10U // 10ms -> 100Hz
#define DIAPHRAGM_ON_TICKS       5U // 50% Duty cycle giúp bơm chạy chậm lại

// ─── Bật/Tắt bơm màng — CHỈ set/clear cờ, TIM6 ISR mới kéo GPIO ────────────
void Pump_Water_In(bool state) {
    bom_cap_enabled = state ? 1U : 0U;
    if (!state) {
        bom_cap_tick = 0;
        HAL_GPIO_WritePin(BOM_CAP_GPIO_Port, BOM_CAP_Pin, RELAY_OFF);
    }
}

void Pump_Water_Out(bool state) {
    bom_xa_enabled = state ? 1U : 0U;
    if (!state) {
        bom_xa_tick = 0;
        HAL_GPIO_WritePin(BOM_XA_GPIO_Port, BOM_XA_Pin, RELAY_OFF);
    }
}

// Dòng này đã được thay thế ở khối trên. Để lại chú thích này để tránh lỗi dòng.

// ─── Bật/Tắt bơm nhu động — CHỈ set/clear cờ, TIM6 ISR mới kéo GPIO ────────
void Pump_Reagent_1(bool state) {
    lo1_enabled = state ? 1U : 0U;
    if (!state) {
        lo1_tick = 0;
        HAL_GPIO_WritePin(LO_1_GPIO_Port, LO_1_Pin, RELAY_OFF); // Tắt ngay lập tức
    }
}

void Pump_Reagent_2(bool state) {
    lo2_enabled = state ? 1U : 0U;
    if (!state) {
        lo2_tick = 0;
        HAL_GPIO_WritePin(LO_2_GPIO_Port, LO_2_Pin, RELAY_OFF); // Tắt ngay lập tức
    }
}

// ─── Reset bộ đếm giọt ──────────────────────────────────────────────────────
void Reset_Drop_Counters(void) {
    count_drop_1 = 0;
    count_drop_2 = 0;
}

// ─── Engine Software PWM — Gọi từ HAL_TIM_PeriodElapsedCallback (mỗi 1ms) ──
void Pump_System_TIM6_Callback(void) {
    // --- LO_1 (Bơm nhu động) ---
    if (lo1_enabled) {
        lo1_tick++;
        if (lo1_tick >= PUMP_PERIOD_TICKS) {
            lo1_tick = 0; // Reset chu kỳ
        }
        HAL_GPIO_WritePin(LO_1_GPIO_Port, LO_1_Pin,
                          (lo1_tick < PUMP_ON_TICKS) ? RELAY_ON : RELAY_OFF);
    }

    // --- LO_2 (Bơm nhu động) ---
    if (lo2_enabled) {
        lo2_tick++;
        if (lo2_tick >= PUMP_PERIOD_TICKS) {
            lo2_tick = 0;
        }
        HAL_GPIO_WritePin(LO_2_GPIO_Port, LO_2_Pin,
                          (lo2_tick < PUMP_ON_TICKS) ? RELAY_ON : RELAY_OFF);
    }
    
    // --- BOM_CAP (Bơm màng cấp) ---
    if (bom_cap_enabled) {
        bom_cap_tick++;
        if (bom_cap_tick >= DIAPHRAGM_PERIOD_TICKS) {
            bom_cap_tick = 0;
        }
        HAL_GPIO_WritePin(BOM_CAP_GPIO_Port, BOM_CAP_Pin,
                          (bom_cap_tick < DIAPHRAGM_ON_TICKS) ? RELAY_ON : RELAY_OFF);
    }

    // --- BOM_XA (Bơm màng xả) ---
    if (bom_xa_enabled) {
        bom_xa_tick++;
        if (bom_xa_tick >= DIAPHRAGM_PERIOD_TICKS) {
            bom_xa_tick = 0;
        }
        HAL_GPIO_WritePin(BOM_XA_GPIO_Port, BOM_XA_Pin,
                          (bom_xa_tick < DIAPHRAGM_ON_TICKS) ? RELAY_ON : RELAY_OFF);
    }
}

// ─── Ngắt EXTI đếm giọt ITR9608 (PA0 → LO_1, PC2 → LO_2) ─────────────────
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == Drop_sen1_Pin) {
        count_drop_1++;
    } else if (GPIO_Pin == Drop_sen2_Pin) {
        count_drop_2++;
    }
}
