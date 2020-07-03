/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define SST_D0_Pin GPIO_PIN_0
#define SST_D0_GPIO_Port GPIOC
#define SST_D1_Pin GPIO_PIN_1
#define SST_D1_GPIO_Port GPIOC
#define SST_D2_Pin GPIO_PIN_2
#define SST_D2_GPIO_Port GPIOC
#define SST_D3_Pin GPIO_PIN_3
#define SST_D3_GPIO_Port GPIOC
#define SST_WE_Pin GPIO_PIN_0
#define SST_WE_GPIO_Port GPIOA
#define SST_A17_Pin GPIO_PIN_1
#define SST_A17_GPIO_Port GPIOA
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define LD2_Pin GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define SST_A9_Pin GPIO_PIN_7
#define SST_A9_GPIO_Port GPIOA
#define SST_D4_Pin GPIO_PIN_4
#define SST_D4_GPIO_Port GPIOC
#define SST_D5_Pin GPIO_PIN_5
#define SST_D5_GPIO_Port GPIOC
#define SST_A0_Pin GPIO_PIN_0
#define SST_A0_GPIO_Port GPIOB
#define SST_A1_Pin GPIO_PIN_1
#define SST_A1_GPIO_Port GPIOB
#define SST_A2_Pin GPIO_PIN_2
#define SST_A2_GPIO_Port GPIOB
#define SST_A15_Pin GPIO_PIN_10
#define SST_A15_GPIO_Port GPIOB
#define SST_A16_Pin GPIO_PIN_12
#define SST_A16_GPIO_Port GPIOB
#define SST_A8_Pin GPIO_PIN_13
#define SST_A8_GPIO_Port GPIOB
#define SST_A13_Pin GPIO_PIN_14
#define SST_A13_GPIO_Port GPIOB
#define SST_A14_Pin GPIO_PIN_15
#define SST_A14_GPIO_Port GPIOB
#define SST_D6_Pin GPIO_PIN_6
#define SST_D6_GPIO_Port GPIOC
#define SST_D7_Pin GPIO_PIN_7
#define SST_D7_GPIO_Port GPIOC
#define SST_CE_Pin GPIO_PIN_8
#define SST_CE_GPIO_Port GPIOC
#define SST_OE_Pin GPIO_PIN_8
#define SST_OE_GPIO_Port GPIOA
#define SST_A11_Pin GPIO_PIN_9
#define SST_A11_GPIO_Port GPIOA
#define SST_A10_Pin GPIO_PIN_10
#define SST_A10_GPIO_Port GPIOA
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SPI3_SS_Pin GPIO_PIN_2
#define SPI3_SS_GPIO_Port GPIOD
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define SST_A3_Pin GPIO_PIN_4
#define SST_A3_GPIO_Port GPIOB
#define SST_A4_Pin GPIO_PIN_5
#define SST_A4_GPIO_Port GPIOB
#define SST_A5_Pin GPIO_PIN_6
#define SST_A5_GPIO_Port GPIOB
#define SST_A6_Pin GPIO_PIN_7
#define SST_A6_GPIO_Port GPIOB
#define SST_A7_Pin GPIO_PIN_8
#define SST_A7_GPIO_Port GPIOB
#define SST_A12_Pin GPIO_PIN_9
#define SST_A12_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
