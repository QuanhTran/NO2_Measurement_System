#ifndef MODULE_FLUIDICS_H
#define MODULE_FLUIDICS_H

#include "main.h"

// Khai báo biến external để file main có thể đọc được số giọt
extern volatile uint8_t count_drop_1;
extern volatile uint8_t count_drop_2;

// Khai báo các hàm
void Pump_Water_In(bool state);
void Pump_Water_Out(bool state);
void Pump_Reagent_1(bool state);
void Pump_Reagent_2(bool state);
void Reset_Drop_Counters(void);

#endif