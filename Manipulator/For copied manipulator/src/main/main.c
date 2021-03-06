#include "stm32f10x.h"
#include "stdio.h"

#define USART1_PORT_Rx GPIOA
#define USART_PIN_Rx GPIO_Pin_10
#define SERVO_PORT GPIOC
#define SERVO_1_PIN GPIO_Pin_6
#define SERVO_2_PIN GPIO_Pin_8
#define SERVO_3_PIN GPIO_Pin_9

#define SERVO_60 2200 // Заполнение ШИМа, соответствующее максимальному повороту сервопривода
#define SERVO_0 900   // Заполнение ШИМа, соответствующее минимальному повороту ШИМа

#define SERVO_MAX_ANGLE 60
#define SERVO_SHORTEST_PULSE 45
#define MAX_DATA 255            // Максимально принятое значени по USART

#define Init_Position 1650 // Заполнение ШИМа, соответствующее начальному положению

#define TIMER_PRESCALER 72
#define TIMER_PERIOD 20000

// Упрощенное обозначение каналов таймера под сервоприводы
#define SERVO_1 TIM3->CCR1
#define SERVO_2 TIM3->CCR3
#define SERVO_3 TIM3->CCR4

/*
Задаем частоту работы таймера: 1 МГц, что соответствет периоду в 1 мкс.
Таймер считает с частотой 1 МГц, отсчет ведется каждые 1 мкс.
Для этого предделитель дожен быть равен 72. Ибо 72000000/72 = 1000000 = 1 МГц.

Необходимо задать период ШИМ в 20 мс, с частотой 50 Гц.
Что есть 20000 отрезков по 1 мкс.
Следовательно период 20 мс(50 Гц).

Ширина управляющего импульса от 900 мкс (0°) до 2200 мкс (60°)

Частота импульсов переменной ширины: 20 мс(50 Гц)

Диапазон вращения: 60`

Класть в регистр сравнения число, равное = значение резистора * (1300 мкс / 255) + начальное значение ШИМа.

Регистр сравнения лучше обновлять строго в момент окончания периода во избежание дёргания сервы

Используется TIM3_Channel(1/3/4)
Максимальное значение, принятое по USART1 - 255
*/

// Local functions prototype
void usart_init (void);
void dma_usart_init (void);
void Initial_Pos(void);
void Set_Pin(void);
void Set_Timer(void);
void Motion(uint8_t *angle);
void Delay(uint32_t Time);

// Массив принятых значений с USART1
static volatile uint8_t USART_buffer[] = {120, 200, 0};

void usart_init (void)
{
  /* Enable USART1 and GPIOA PORT */
  RCC_APB2PeriphClockCmd (RCC_APB2Periph_GPIOA, ENABLE); //Разрешить тактирование ножки Rx
  RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART1, ENABLE); //Разрешить тактирование USART1
  /* Configure the GPIOs */
  GPIO_InitTypeDef GPIO_InitStructure;

  /* Configure USART1 Rx (PA.10) as input floating */
  GPIO_InitStructure.GPIO_Pin = USART_PIN_Rx;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
  GPIO_Init(USART1_PORT_Rx, &GPIO_InitStructure);

  /* Configure the USART1 */
  USART_InitTypeDef USART_InitStructure;                                          // Структура для инициализации порта USART
  USART_InitStructure.USART_BaudRate = 115200;                                    // Скорость передачи данных
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;                     // Количество передаваемых бит в кадре
  USART_InitStructure.USART_StopBits = USART_StopBits_1;                          // Количество стоп-битов 1
  USART_InitStructure.USART_Parity = USART_Parity_No;                             // Проверка четности - отключена
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; // Аппаратное управление потоком - выключено
  USART_InitStructure.USART_Mode = USART_Mode_Rx;                                 // Включение режима передачи данных
  USART_Init(USART1, &USART_InitStructure);                                       // Инициализация порта USART1
  USART_Cmd(USART1, ENABLE);                                                      
}
// Настройка модуля ДМА для забора данных, полученных по USART1
void dma_usart_init (void)
{
  // Разрешение тактирвоания модуля DMA
  RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE); 
  
  // Настройка модуля DMA1 - c USART1 в буфер  
  DMA_InitTypeDef DMA_InitStructure;                                      // Структура для инициализации модуля DMA
  DMA_StructInit(&DMA_InitStructure);                                     // Заполнение структуры по умолчанию
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;                 // Инкрементировать указателя на данные в памяти
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         // Разммер единицы данных в памяти
  DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) USART_buffer;         // Адрес области памяти, куда необходимо положить данные (адрес нулевого элемента массива)
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;        // Не инкрементировать указателя на данные в периферии
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // Размер единицы данных в перфиерии
  DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) &(USART1->DR);    // Адрес регистра данных USART1
  DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                      // Периферия является приемником назначения
  DMA_InitStructure.DMA_BufferSize = sizeof(USART_buffer);                // Размер буффера
  DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;                         // Режим работы - по кругу (циклический)
  DMA_Init(DMA1_Channel5, &DMA_InitStructure);	                          // 4 канал DMA1 - Tx USART1
  DMA_Cmd(DMA1_Channel5, ENABLE);                                         // Включаем прямой доступ к памяти DMA
  USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);                          // Активируем передачу в последовательный порт по запросу DMA 

  // Установка прерываний от DMA по окончании приема 
  DMA_ITConfig(DMA1_Channel5, DMA_IT_TC, ENABLE);
  NVIC_EnableIRQ(DMA1_Channel5_IRQn);  
}
// Обработчик прерывания по событию - DMA1 сохранил показания всех 3-х резисторов, переданных по USART1
void DMA1_Channel5_IRQHandler(void)
{
  Motion(USART_buffer);
}
// функция временной задержки
volatile uint32_t TimingDelay;
void Delay(uint32_t Time) 
{
	TimingDelay = Time;
	while(TimingDelay != 0);
}

void TimingDelay_Decrement(void)
{
	if (TimingDelay != 0)
		{
			TimingDelay--;
		}
}
// system timer interrupt handler
void SysTick_Handler(void)                
{
	TimingDelay_Decrement();
}
 // Настройка подключения периферии
void Set_Pin()                                          
{
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC , ENABLE); // Тактируем порт С, к которому подключены сервоприводы
  // Настройка ножек, к которым подключены сервоприводы, в режим Alternate function
  GPIO_InitTypeDef GPIO_InitStructure;                 
  GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;                         // Необходим альтернативный режим, т.к. управлять сервоприводами будем с помощью ШИМа
	GPIO_InitStructure.GPIO_Pin = SERVO_1_PIN | SERVO_2_PIN | SERVO_3_PIN;  // Настройка ножек к которым подключены сервоприводы в режим Alternate function
  GPIO_Init(SERVO_PORT, &GPIO_InitStructure);
}

// Настройка таймера
void Set_Timer()
{
  // Включаем альтернативный режим
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
  GPIO_PinRemapConfig(GPIO_FullRemap_TIM3, ENABLE);
	//Настройка базового таймера
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);       // Тактируем Таймер 3                               
	TIM_TimeBaseInitTypeDef TIM;                               // Создаем экземпляр структуры, которая задает частоту через предделитель
  TIM_TimeBaseStructInit(&TIM);                              // Заполняем структуру значениями по умолчанию
	TIM.TIM_Prescaler = TIMER_PRESCALER;                       // Задаем предделитель таймера
	TIM.TIM_Period = TIMER_PERIOD;                             // Период ШИМ, такой, чтобы частота 50 Гц.
	TIM_TimeBaseInit(TIM3, &TIM);                              // Инициализируем таймер
	
	// настройка ШИМа. Настройка канала.  начальное заполнение: 90 тиков (900 мкс)
	TIM_OCInitTypeDef TIM_PWM;                                  // Конфигурация выхода таймера. Структура для генерации ШИМ
	TIM_OCStructInit(&TIM_PWM);
	TIM_PWM.TIM_OCMode = TIM_OCMode_PWM1;                       // Конфигурируем выхода таймера в режим PWM1. Помимо PWM1 есть еще PWM2. Это всего лишь разные режимы ШИМ – с выравниванием по границе и по центру
	TIM_PWM.TIM_OutputState = TIM_OutputState_Enable;           // Включение выход
	TIM_PWM.TIM_Pulse = 0;                                      // Значение, с которым будет сравниваться
	TIM_OC1Init(TIM3, &TIM_PWM);                                // Конфигурация 1-го канала (Поворотная ось основания)
  TIM_OC3Init(TIM3, &TIM_PWM);                                // Конфигурация 3-го канала (Плечо)
  TIM_OC4Init(TIM3, &TIM_PWM);                                // Конфигурация 4-го канала (Предплечье)
  	
	// разрешение прерываний и работы таймера
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);                  // Режим генерации прерывания по обновлению, переполнению
	TIM_Cmd(TIM3, ENABLE);                                      // Запуск таймера
	
	// настройка системного таймера
	__disable_irq();
	SysTick_Config(SystemCoreClock / 1000);
	__enable_irq();
}
// Функция установки манипулятора в начальное положение
void Initial_Pos(void)
{
  SERVO_1 = Init_Position;
  SERVO_2 = Init_Position;
  SERVO_3 = Init_Position;
}
// Функция задания позиции манипулятора
volatile uint16_t pulse = 0;
void Motion(uint8_t *data_from_usart) 
{
  pulse = data_from_usart[0]*(SERVO_60 - SERVO_0) / MAX_DATA; // Расчет отклонения положения сервопривода от нулевого положения
  TIM_SetCompare1(TIM3, pulse);
  SERVO_1 = pulse + SERVO_0;

  pulse = data_from_usart[1]*(SERVO_60 - SERVO_0) / MAX_DATA;
  TIM_SetCompare3(TIM3, pulse);
  SERVO_2 = pulse + SERVO_0;
  
  pulse = data_from_usart[2]*(SERVO_60 - SERVO_0) / MAX_DATA;
  TIM_SetCompare4(TIM3, pulse);
  SERVO_3 = pulse + SERVO_0;
  for (int i = 0; i < 10000; i++);
}

int main(void)
{
  usart_init();
  dma_usart_init();
  Set_Pin();
	Set_Timer();
  Delay(500);
  Initial_Pos();
  Delay(1500);
	while(1)
	{
	}
}

// классический ассерт для STM32
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t * file, uint32_t line)
{ 
    /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
     
    (void)file;
    (void)line;

    __disable_irq();
    while(1)
    {
        // это ассемблерная инструкция "отладчик, стой тут"
        // если вы попали сюда, значит вы ошиблись в параметрах. Смотрите в call stack
        __BKPT(0xAB);
    }
}
#endif
