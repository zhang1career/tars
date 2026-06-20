/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_otg.h"
#include "shell.h"
#include "lcd_log.h"
#include "tars_app.h"
#include "tars_lua.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
osThreadId defaultTaskHandle;
osThreadId shellTaskHandle;
/* USER CODE BEGIN Variables */
osThreadId eluaTaskHandle;
osThreadId schedulerTaskHandle;
/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
void StartDefaultTask(void const * argument);
void StartShellTask(void const * argument);
/* USER CODE BEGIN FunctionPrototypes */
void StartEluaTask(void const * argument);
void StartSchedulerTask(void const * argument);
/* USER CODE END FunctionPrototypes */

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* Hook prototypes */
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 2 */
__weak void vApplicationIdleHook( void )
{
}
/* USER CODE END 2 */

/* USER CODE BEGIN 4 */
__weak void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
__weak void vApplicationMallocFailedHook(void)
{
}
/* USER CODE END 5 */

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* Create the thread(s) */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 2048);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  osThreadDef(shellTask, StartShellTask, osPriorityBelowNormal, 0, 2048);
  shellTaskHandle = osThreadCreate(osThread(shellTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  osThreadDef(eluaTask, StartEluaTask, osPriorityLow, 0, 6144);
  eluaTaskHandle = osThreadCreate(osThread(eluaTask), NULL);

  osThreadDef(schedulerTask, StartSchedulerTask, osPriorityLow, 0, 4096);
  schedulerTaskHandle = osThreadCreate(osThread(schedulerTask), NULL);
  /* USER CODE END RTOS_THREADS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  (void)argument;
  LcdLog_Init();
  UsbOtg_Task(argument);
}

void StartShellTask(void const * argument)
{
  Shell_Task(argument);
}

/* USER CODE BEGIN Application */
void StartEluaTask(void const * argument)
{
  TarsLua_Task(argument);
}

void StartSchedulerTask(void const * argument)
{
  TarsApp_SchedulerTask(argument);
}
/* USER CODE END Application */
