#include "module_fluidics.h"

// Biến toàn cục đếm giọt
volatile uint8_t count_drop_1 = 0;
volatile uint8_t count_drop_2 = 0;

// Trạng thái điều khiển Mosfet kích mức cao (Active-High)
#define RELAY_ON  GPIO_PIN_SET
#define RELAY_OFF GPIO_PIN_RESET

// Hàm điều khiển Bơm màng cấp nước
void Pump_Water_In(bool state) {
    HAL_GPIO_WritePin(BOM_CAP_GPIO_Port, BOM_CAP_Pin, state ? RELAY_ON : RELAY_OFF);
}

// Hàm điều khiển Bơm nhu động nhỏ Lọ 1
void Pump_Reagent_1(bool state) {
    HAL_GPIO_WritePin(LO_1_GPIO_Port, LO_1_Pin, state ? RELAY_ON : RELAY_OFF);
}

// Hàm điều khiển Bơm nhu động nhỏ Lọ 2
void Pump_Reagent_2(bool state) {
    HAL_GPIO_WritePin(LO_2_GPIO_Port, LO_2_Pin, state ? RELAY_ON : RELAY_OFF);
}

// Hàm điều khiển Bơm màng xả nước thải
void Pump_Water_Out(bool state) {
    HAL_GPIO_WritePin(BOM_XA_GPIO_Port, BOM_XA_Pin, state ? RELAY_ON : RELAY_OFF);
}

// Reset bộ đếm giọt
void Reset_Drop_Counters(void) {
    count_drop_1 = 0;
    count_drop_2 = 0;
}

// Hàm ngắt đếm giọt ITR9608 (PA0 và PC2)
extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == Drop_sen1_Pin) {
        count_drop_1++;       // Lọ 1
    } else if (GPIO_Pin == Drop_sen2_Pin) {
        count_drop_2++;       // Lọ 2
    }
}
