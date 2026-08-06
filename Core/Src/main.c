/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
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
#include "main.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include "csb.h"
#include "usr_uart.h"
#include "console.h"
#include "lcd.h"
#include "BH1750.h"
#include "bmp280.h"
#include "SCD04.h"
#include "DHT11.h"
#include "EB21.h"
#include "gnss.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t collect_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
bool key_1_touch = false;
bool key_2_touch = false; // KEY2 按下时的事件标志，在中断里置 true，key_function 中清�????
bool key_3_touch = false;
bool fs_status = false;
bool lcd_status = false;
int now_page = 0; // LCD 当前显示页面索引 now_page�????0=�????0页，1=�????1页，在主循环切换逻辑中更�????

#define BH1750_TIMER_PERIOD 3
#define DHT11_TIMER_PERIOD 10
#define SCD04_TIMER_PERIOD 5
#define RFID_TIMER_PERIOD 2
#define CO2_READ_TIMER_PERIOD 1
static int bh1750_count_timer = BH1750_TIMER_PERIOD;
static int dht11_count_timer = DHT11_TIMER_PERIOD;
static int scd04_count_timer = SCD04_TIMER_PERIOD;
static int rfid_count_timer = RFID_TIMER_PERIOD;

enum
{
    COLLECT = 0,
    READ = 1,
};

static int co2_status = COLLECT;
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    unsigned int retry = 0;
    if (htim == &htim2)
    {
        bh1750_count_timer--;
        dht11_count_timer--;
        scd04_count_timer--;
        rfid_count_timer--;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) // 串口1
    {
        queue_in((char *)&dataRcvd);
        HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);
        return;
    }

    if (huart == &huart3) // LCD
    {
        lcd_buffer[lcd_count++] = lcd_temp;
        if (lcd_count == LCD_REPLAY_LEN)
        {
            lcd_check_frame(); // 查看LCD是否准备就绪
        }
        HAL_UART_Receive_IT(&huart3, &lcd_temp, 1);
        return;
    }

    if (huart == &huart4)
    {
        if (gnss_temp == 0x24)
        {
            gnss_frame_ready = true;
        }

        if (gnss_frame_ready)
        {
            gnss_buffer[gnss_count++] = gnss_temp;
            if (gnss_count == GNSS_DATA_HEAD_LEN)
            {
                if (!gnss_check_frame())
                {
                    gnss_count = 0;
                    gnss_frame_ready = false;
                }
            }
            else if (gnss_temp == 0x0D)
            {
                gnss_deal_buffer();
            }
        }

        HAL_UART_Receive_IT(&huart4, &gnss_temp, 1);
        return;
    }

    if (huart == &huart5)
    {
        if (csb_temp == 0xFF)
        {
            csb_frame_ready = true;
        }

        if (csb_frame_ready)
        {
            csb_buffer[csb_count++] = csb_temp;
            if (csb_count == CSB_FRAME_LEN)
            {
                csb_deal_buffer();
            }
        }

        HAL_UART_Receive_IT(&huart5, &csb_temp, 1);
        return;
    }

    if (huart == &huart7)
    {
        xs_buffer[xs_count++] = xs_temp;
        HAL_UART_Receive_IT(&huart7, &xs_temp, 1);
        return;
    }

    if (huart == &huart10)
    {
        if (bmp280_temp == 0x50)
        {
            bmp280_frame_ready = true;
        }

        if (bmp280_frame_ready)
        {
            bmp280_buffer[bmp280_count++] = bmp280_temp;
            if (bmp280_temp == 0x0D)
            {
                bmp280_deal_buffer();
            }
        }

        HAL_UART_Receive_IT(&huart10, &bmp280_temp, 1);
        return;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t key3_tick = 0;
    static bool touch_3 = true;
    switch (GPIO_Pin)
    {
    case KEY3_Pin:
        if (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET)
        {
            // KEY3 松开逻辑,上升沿触
            key_3_touch = true;
            if (touch_3)
            {
                HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_SET);
                HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_SET);
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 99);
                fs_status = true;
                lcd_status = true;
            }
            else
            {
                HAL_GPIO_WritePin(YELLOW_GPIO_Port, YELLOW_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(GREEN_GPIO_Port, GREEN_Pin, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(RED_GPIO_Port, RED_Pin, GPIO_PIN_RESET);
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
                fs_status = false;
                lcd_status = false;
            }
            touch_3 = !touch_3;
        }
        break;
    case KEY2_Pin:
        // 读取引脚确认是�?�按下�??(低电�????)，消抖并确认硬件状�??
        if (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET)
        {
            // KEY2 按下逻辑：仅置事件标志，真正的页面切换在 key_function() 中执�????
            key_2_touch = true;
        }
        break;
    case KEY1_Pin:
        // 读取引脚确认是�?�按下�??(低电�????)，消抖并确认硬件状�??
        if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET)
        {
            // KEY1 按下逻辑：仅置事件标志，真正的页面切换在 key_function() 中执�????
            key_1_touch = true;
        }
        break;
    }
}

void lcd_set_page0(void)
{
    char temp[256] = {0};
    // 终端协议：SET_TXT(行号,'内容');  显示温湿度与光照
    // sprintf(temp, "SET_TXT(4,'10.5\xA1\xE3\x43');SET_TXT(5,'10.6RH');SET_TXT(6,'888lx');\r\n");
    sprintf(temp, "SET_TXT(4,'%.1f\xA1\xE3\x43');SET_TXT(5,'%.1fRH');SET_TXT(6,'%dlx');\r\n", tem, hum, lx_Data);
    lcd_set_data(temp); // 通过 USART3 下发本页文本
}

void lcd_set_page1(void)
{
    char temp[256] = {0};
    sprintf(temp, "SET_TXT(4,'%s');SET_TXT(5,'%s');SET_TXT(6,'%ldPa');SET_TXT(7,'%ldPPM');\r\n",
            lcd_status > 0 ? "open" : "close",
            fs_status > 0 ? "open" : "close",
            pressure_Data,
            data_co2);
    lcd_set_data(temp);
}

void key_function(void)
{
    const int PAGE_COUNT = 2; /* 页面总数，用于循环切�???? */

    /* KEY1 处理：切换到上一�???? */
    if (key_1_touch)
    {
        int target = (now_page - 1 + PAGE_COUNT) % PAGE_COUNT; /* 计算目标页面（循环） */
        lcd_switch(target);
        if (target)
        {
            lcd_set_page1();
            now_page = 1;
        }
        else
        {
            lcd_set_page0();
            now_page = 0;
        }
        key_1_touch = false; /* 清除标志 */
    }

    /* KEY2 处理：切换到下一�???? */
    if (key_2_touch)
    {
        int target = (now_page + 1) % PAGE_COUNT; /* 计算目标页面（循环） */
        lcd_switch(target);
        if (target)
        {
            lcd_set_page1();
            now_page = 1;
        }
        else
        {
            lcd_set_page0();
            now_page = 0;
        }
        key_2_touch = false; /* 清除标志 */
    }
}

static void timer_collect_data(void)
{
    if (dht11_count_timer < 0)
    {
        HAL_TIM_Base_Stop_IT(&htim2);
        DHT11_Data(&tem, &hum);
        dht11_count_timer = DHT11_TIMER_PERIOD;
        goto end;
    }

    if (scd04_count_timer < 0)
    {
        HAL_TIM_Base_Stop_IT(&htim2);
        if (co2_status == COLLECT)
        {
            scd04_collect();
            scd04_count_timer = SCD04_TIMER_PERIOD;
            co2_status = READ;
        }
        else
        {
            co2_status = COLLECT;
            scd04_read_data();
            scd04_count_timer = CO2_READ_TIMER_PERIOD;
        }

        goto end;
    }

    if (bh1750_count_timer < 0)
    {
        HAL_TIM_Base_Stop_IT(&htim2);
        bh1750_count_timer = BH1750_TIMER_PERIOD;
        bh7150_get_data();

        goto end;
    }

    return;

end:
    if (now_page)
    {
        lcd_set_page1();
    }
    else
    {
        lcd_set_page0();
    }
    HAL_TIM_Base_Start_IT(&htim2);
}

static void get_sensor_data(void)
{
    // 先采集二氧化碳，这个传感器有延时，这样其他传感器有一个初始化的时�?????
    HAL_Delay(3000);
    scd04_get_Data();
    DHT11_Data(&tem, &hum);
    bh7150_get_data();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_I2C3_Init();
  MX_USART10_UART_Init();
  MX_I2C2_Init();
  MX_TIM3_Init();
  MX_UART4_Init();
  MX_UART5_Init();
  MX_UART7_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
    SCD04_INIT();
    BH1750_INIT();

    HAL_UART_Receive_IT(&huart1, &dataRcvd, 1);
    HAL_UART_Receive_IT(&huart3, &lcd_temp, 1);
    HAL_UART_Receive_IT(&huart4, &gnss_temp, 1);
    HAL_UART_Receive_IT(&huart5, &csb_temp, 1);
    HAL_UART_Receive_IT(&huart7, &xs_temp, 1);
    HAL_UART_Receive_IT(&huart10, &bmp280_temp, 1);
    HAL_TIM_Base_Start_IT(&htim2);

    console_init();

    BH7150_cmd_register();
    bmp280_cmd_register();
    csb_cmd_register();
    dht11_cmd_register();
    xs_cmd_register();
    gnss_cmd_register();
    lcd_cmd_register();
    SCD04_cmd_register();

    get_sensor_data();
    lcd_set_page0();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
        console_run();
        key_function();
        timer_collect_data();
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

