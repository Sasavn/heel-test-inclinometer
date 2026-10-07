/*
 * main.h для host-тестов: минимум типов и функций HAL, которые используют
 * app.c, bwm427.c и sd_logger.c. Реализация — tests/host/fake_hal.c.
 */
#ifndef __MAIN_H
#define __MAIN_H

#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;

typedef struct { int dummy; } GPIO_TypeDef;
extern GPIO_TypeDef fake_gpioa, fake_gpiob, fake_gpioc;

#define LED_Pin              0x2000
#define LED_GPIO_Port        (&fake_gpioc)
#define SW_RECORD_Pin        0x0008
#define SW_RECORD_GPIO_Port  (&fake_gpioa)
#define RS485_RE_Pin         0x1000
#define RS485_RE_GPIO_Port   (&fake_gpiob)
#define RS485_DE_Pin         0x0100
#define RS485_DE_GPIO_Port   (&fake_gpioa)
#define SD_CS_GPIO_Port      (&fake_gpioa)
#define SD_CS_Pin            0x0010

// UART
#define HAL_UART_STATE_READY 0x20u
typedef struct {
	uint32_t BaudRate;
} UART_InitTypeDef;
typedef struct __UART_HandleTypeDef {
	void *Instance;
	UART_InitTypeDef Init;
	volatile uint32_t RxState;
} UART_HandleTypeDef;
#define __HAL_UART_CLEAR_OREFLAG(h) ((void) (h))

// SPI: делители BR (драйвер SD в host-тестах обменивается через fake_sd.c)
#define SPI_BAUDRATEPRESCALER_4   0x08u
#define SPI_BAUDRATEPRESCALER_128 0x30u

// ADC
typedef struct { int dummy; } ADC_HandleTypeDef;
typedef struct {
	uint32_t Channel;
	uint32_t Rank;
	uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;
#define ADC_CHANNEL_1             1u
#define ADC_CHANNEL_VREFINT       17u
#define ADC_SAMPLETIME_480CYCLES  7u
#define ADC_FLAG_EOC              2u
#define __HAL_ADC_GET_FLAG(h, f)  fake_adc_eoc()

// I2C
typedef struct { int dummy; } I2C_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT      1u

// Ядро
typedef struct {
	volatile uint32_t CYCCNT;   // счётчик тактов: двигают тесты
} fake_dwt_t;
extern fake_dwt_t fake_dwt;
#define DWT                   (&fake_dwt)
#define __DMB()               ((void) 0)
#define __disable_irq()       ((void) 0)
#define __get_PRIMASK()       0u
#define __set_PRIMASK(x)      ((void) (x))

uint32_t HAL_GetTick(void);
extern uint32_t SystemCoreClock;
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortReceive_IT(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_AbortTransmit_IT(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *hadc, ADC_ChannelConfTypeDef *c);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *hadc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *hadc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *hadc);
int fake_adc_eoc(void);

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr, uint32_t trials, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t regsize, uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t addr, uint16_t reg, uint16_t regsize, uint8_t *data, uint16_t size, uint32_t timeout);

void Error_Handler(void);

#endif
