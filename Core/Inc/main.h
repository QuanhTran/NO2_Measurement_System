/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Drop_sen2_Pin GPIO_PIN_2
#define Drop_sen2_GPIO_Port GPIOC
#define Drop_sen2_EXTI_IRQn EXTI2_IRQn
#define Drop_sen1_Pin GPIO_PIN_0
#define Drop_sen1_GPIO_Port GPIOA
#define Drop_sen1_EXTI_IRQn EXTI0_IRQn
#define LED_Pin GPIO_PIN_2
#define LED_GPIO_Port GPIOA
#define BTN_START_Pin GPIO_PIN_7
#define BTN_START_GPIO_Port GPIOE
#define STEP_PIN_Pin GPIO_PIN_11
#define STEP_PIN_GPIO_Port GPIOC
#define DIR_PIN_Pin GPIO_PIN_0
#define DIR_PIN_GPIO_Port GPIOD
#define EN_PIN_Pin GPIO_PIN_2
#define EN_PIN_GPIO_Port GPIOD
#define BOM_CAP_Pin GPIO_PIN_3
#define BOM_CAP_GPIO_Port GPIOD
#define LO_1_Pin GPIO_PIN_4
#define LO_1_GPIO_Port GPIOD
#define LO_2_Pin GPIO_PIN_5
#define LO_2_GPIO_Port GPIOD
#define BOM_XA_Pin GPIO_PIN_6
#define BOM_XA_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */

/* LED đo màu — TIM2 CH3 (PA2), f_PWM = 10kHz, ARR = 99
 * Duty cycle = Pulse / (ARR + 1) * 100% */
#define LED_PWM_ON   80U   /* 80% — đủ sáng để đo quang phổ */
#define LED_PWM_OFF   0U   /* 0%  — tắt hoàn toàn            */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
