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
#include "stm32h7xx_hal.h"

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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BATT_VOLTAGE_IN_Pin GPIO_PIN_1
#define BATT_VOLTAGE_IN_GPIO_Port GPIOC
#define VSS_INPUT_Pin GPIO_PIN_0
#define VSS_INPUT_GPIO_Port GPIOA
#define OIL_PRESSURE_IN_Pin GPIO_PIN_1
#define OIL_PRESSURE_IN_GPIO_Port GPIOA
#define COOLANT_TEMP_IN_Pin GPIO_PIN_2
#define COOLANT_TEMP_IN_GPIO_Port GPIOA
#define DIMMER_IN_Pin GPIO_PIN_12
#define DIMMER_IN_GPIO_Port GPIOF
#define TACH_INPUT_Pin GPIO_PIN_9
#define TACH_INPUT_GPIO_Port GPIOE
#define TURN_LEFT_IN_Pin GPIO_PIN_12
#define TURN_LEFT_IN_GPIO_Port GPIOB
#define TURN_RIGHT_IN_Pin GPIO_PIN_13
#define TURN_RIGHT_IN_GPIO_Port GPIOB
#define BT_TX_Pin GPIO_PIN_14
#define BT_TX_GPIO_Port GPIOB
#define BT_RX_Pin GPIO_PIN_15
#define BT_RX_GPIO_Port GPIOB
#define LCD_BACKLIGHT_Pin GPIO_PIN_8
#define LCD_BACKLIGHT_GPIO_Port GPIOD
#define HIGHBEAM_IN_Pin GPIO_PIN_9
#define HIGHBEAM_IN_GPIO_Port GPIOD
#define ST77916_CS1_Pin GPIO_PIN_11
#define ST77916_CS1_GPIO_Port GPIOD
#define ST77916_CS2_Pin GPIO_PIN_12
#define ST77916_CS2_GPIO_Port GPIOD
#define ST77916_CS3_Pin GPIO_PIN_13
#define ST77916_CS3_GPIO_Port GPIOD
#define ST77916_CS4_Pin GPIO_PIN_2
#define ST77916_CS4_GPIO_Port GPIOG
#define ST77916_RST_Pin GPIO_PIN_3
#define ST77916_RST_GPIO_Port GPIOG
#define NV3052C_CS_Pin GPIO_PIN_4
#define NV3052C_CS_GPIO_Port GPIOG
#define NV3052C_RST_Pin GPIO_PIN_5
#define NV3052C_RST_GPIO_Port GPIOG
#define DEBUG_TX_Pin GPIO_PIN_5
#define DEBUG_TX_GPIO_Port GPIOD
#define DEBUG_RX_Pin GPIO_PIN_6
#define DEBUG_RX_GPIO_Port GPIOD
#define EEPROM_SCL_Pin GPIO_PIN_6
#define EEPROM_SCL_GPIO_Port GPIOB
#define EEPROM_SDA_Pin GPIO_PIN_7
#define EEPROM_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
