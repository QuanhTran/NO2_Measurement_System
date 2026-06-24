#ifndef MODULE_STIRRER_H
#define MODULE_STIRRER_H

#include "main.h"

void Stirrer_Enable(bool state);
void Stirrer_Update(uint32_t speed_delay_us);

#endif