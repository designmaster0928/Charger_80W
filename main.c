/* Includes ------------------------------------------------------------------*/
#include "stm8s_conf.h"
#include "stm8s_tim1.h"
#include "stm8s_clk.h"
#include "delay.h"

/* Private defines -----------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
uint16_t valTemp;
uint16_t valVBat;
uint16_t valISense;

uint16_t timer2Count = 0;
uint8_t  timer2Flag = 0;

#define SPI_PORT        GPIOC
#define SPI_CS          GPIO_PIN_2
#define SPI_SCL         GPIO_PIN_5
#define SPI_MOSI        GPIO_PIN_6
#define SPI_MISO        GPIO_PIN_7        

#define COUNT_LIMIT     100
#define COUNT_LIMIT_FULLCHARGED     4000

enum SYSTEM_STATE {
    FULL_CHARGED = 0,
    CHARGING,
    DISCONNECTED
};

enum SYSTEM_STATE systemState = DISCONNECTED;
unsigned char isMobileSubBoard = 0;

static void CLK_Config(void)
{
    CLK_DeInit();
    CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1);      //f_Master = HSI/1 = 16MHz
    CLK_SYSCLKConfig(CLK_PRESCALER_CPUDIV1);            //f_CPU = f_Master/1 = 16MHz
    while(CLK_GetFlagStatus(CLK_FLAG_HSIRDY)!=SET);     //wait until HSI ready
}

static void delay_ns(unsigned int nano)
{
    volatile unsigned int count = nano;
    while (count--)
      asm("nop");               //62.5nS : 16MHz
}

void SPI_Config(void)
{
  GPIO_Init(GPIOA, GPIO_PIN_2, GPIO_MODE_OUT_PP_LOW_FAST);              // PC4 : SPI_CS
  GPIO_WriteLow(GPIOA, GPIO_PIN_2);   
  GPIO_Init(GPIOC, GPIO_PIN_5, GPIO_MODE_OUT_PP_LOW_FAST);              // PC5 : SPI_SCK
  GPIO_WriteLow(GPIOC, GPIO_PIN_5); 
  GPIO_Init(GPIOC, GPIO_PIN_6, GPIO_MODE_OUT_PP_LOW_FAST);              // PC6 : SPI_MOSI
  GPIO_WriteLow(GPIOC, GPIO_PIN_6); 
  GPIO_Init(GPIOC, GPIO_PIN_7, GPIO_MODE_IN_FL_IT);                     // PC7 : SPI_MISO
}

unsigned char SPI_Read(unsigned char address)
{
    unsigned char readValue = 0;
    unsigned int  readReg = address;
    readReg = 0x300 | readReg;
    unsigned char readBit;
    unsigned char i;
    
    GPIO_WriteLow(GPIOA, SPI_CS);   
    GPIO_WriteLow(SPI_PORT, SPI_SCL);
    GPIO_WriteLow(SPI_PORT, SPI_MOSI);    
    delay_ns(10);         // 1.6MHz
    
    GPIO_WriteHigh(GPIOA, SPI_CS);
    for (i = 0; i < 10; i++)
    {
        GPIO_WriteLow(SPI_PORT, SPI_SCL); 
        delay_ns(10);         // 1.6MHz         
        
        if (readReg & 0x200)
          GPIO_WriteHigh(SPI_PORT, SPI_MOSI);
        else
          GPIO_WriteLow(SPI_PORT, SPI_MOSI);
        GPIO_WriteHigh(SPI_PORT, SPI_SCL);
        delay_ns(10);         // 1.6MHz 
        
        readReg = readReg << 1;
    }
    
    for (i = 0; i < 8; i++)
    {      
      readValue = readValue << 1;
      GPIO_WriteLow(SPI_PORT, SPI_SCL); 
      delay_ns(10);         // 1.6MHz 
      GPIO_WriteHigh(SPI_PORT, SPI_SCL);
      delay_ns(10);         // 1.6MHz 
      readBit = GPIO_ReadInputPin(SPI_PORT, SPI_MISO);
      if (readBit != 0)
      {
        readValue = readValue + 1;
      }
      else
      {
        readValue = readValue + 0;
      }
    }
    
    GPIO_WriteLow(SPI_PORT, SPI_SCL); 
    delay_ns(10);         // 1.6MHz 
    GPIO_WriteLow(GPIOA, SPI_CS);   
    GPIO_WriteHigh(SPI_PORT, SPI_SCL);
    delay_ns(10);         // 1.6MHz 
    GPIO_WriteHigh(GPIOA, SPI_CS);   
    GPIO_WriteLow(SPI_PORT, SPI_SCL);
    
    return readValue;
}


void ADC_Config(void)
{
    //Config GPIO for ADC1    
    GPIO_Init(GPIOC, GPIO_PIN_4 , GPIO_MODE_IN_FL_NO_IT);   //PC4 AIN2 Channel 2
    GPIO_Init(GPIOD, GPIO_PIN_2 , GPIO_MODE_IN_FL_NO_IT);   //PD2 AIN3 Channel 3
    GPIO_Init(GPIOD, GPIO_PIN_3 , GPIO_MODE_IN_FL_NO_IT);   //PD3 AIN4 Channel 4
    
    /* Init ADC */
    ADC1_DeInit();                                          //deinit first configuration of ADC
  
    ADC1_PrescalerConfig(ADC1_PRESSEL_FCPU_D2);
    ADC1_ExternalTriggerConfig(ADC1_EXTTRIG_TIM,DISABLE);
    ADC1_SchmittTriggerConfig(ADC1_SCHMITTTRIG_ALL,DISABLE);

    ADC1_ConversionConfig(ADC1_CONVERSIONMODE_SINGLE, 
                        ADC1_CHANNEL_4, 
                        ADC1_ALIGN_RIGHT);
  
    ADC1_ScanModeCmd(ENABLE);                                //enable scan mode
    ADC1_Cmd(ENABLE);                                        //Turn on ADC1 the value will be store in ADC1_IRQHandler  
}

void AddBufferTemp(uint16_t temp)
{  
    valTemp = temp;
}

void AddBufferVoltage(uint16_t voltage)
{  
    valVBat = voltage;
}

void AddBufferCurrent(uint16_t current)
{ 
    valISense = current;
}

void UART_Config(void){
  //PD5 UART TX
  //PD6 Input of VSEL��Not RX
  UART1_DeInit();
  UART1_Init((uint32_t)115200, 
             UART1_WORDLENGTH_8D, 
             UART1_STOPBITS_1, 
             UART1_PARITY_NO,
             UART1_SYNCMODE_CLOCK_DISABLE, 
             UART1_MODE_TX_ENABLE | UART1_MODE_RX_DISABLE);
}

int putchar (char c)
{
  /* Write a character to the UART1 */
  UART1_SendData8(c);
  /* Loop until the end of transmission */
  while (UART1_GetFlagStatus(UART1_FLAG_TXE) == RESET);

  return (c);
}

void putstring(char* string)
{
    while(*string) // Het chuoi ky tu thi thoat
    {
        putchar(*string);
        string ++; // Lay ky tu tiep theo
    }
    putchar(0);
}

void UartSendInt(unsigned int n)
{
     unsigned char buffer[16];
     unsigned char i,j;

     if(n == 0) {
    	 putchar('0');
          return;
     }

     for (i = 15; i > 0 && n > 0; i--) {
          buffer[i] = (n%10)+'0';
          n /= 10;
     }

     for(j = i+1; j <= 15; j++) {
    	 putchar(buffer[j]);
     }
}
#define PWM_DUTY_NONE           0
#define PWM_DUTY_MOBILE         117
#define PWM_DUTY_RADIO          164

unsigned int  pwmDuty = PWM_DUTY_NONE;

//POD IDs for Radios
//unsigned char dataEEPROM_CP185_CP100D[24]      = {0x3F, 0x06, 0x03, 0x01, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x58, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0x7C, 0xA7};
//unsigned char dataEEPROM_XP3300TRBO[24]        = {0x3E, 0x06, 0x02, 0x01, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x3D, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0x13, 0xBA};
//unsigned char dataEEPROM_CP200[24]             = {0x3D, 0x06, 0x01, 0x00, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x66, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0xAD, 0x83};
//unsigned char dataEEPROM_KW_NX1000[24]         = {0x47, 0x07, 0x01, 0x01, 0x01, 0xEB, 0x07, 0xD0, 0x02, 0x58, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x38, 0x40, 0xA3, 0x44};
//unsigned char dataEEPROM_GO[24]                = {0x29, 0x04, 0x01, 0x01, 0x01, 0xEB, 0x07, 0xD0, 0x02, 0x3D, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x54, 0x60, 0x0D, 0x89};
//
////POD IDs for Phones
//unsigned char dataEEPROM_XP5Plus[24]           = {0x0E, 0x01, 0x02, 0xFF, 0x00, 0xF5, 0x0D, 0xAC, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x55, 0x54, 0x60, 0x8A, 0x02};
//unsigned char dataEEPROM_XP10[24]              = {0x0F, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x13, 0x88, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x7A, 0x01, 0x62, 0x54, 0x60, 0x96, 0x4C};
//unsigned char dataEEPROM_Orion_ros[24]         = {0x1F, 0x03, 0x01, 0x02, 0x00, 0xF5, 0x06, 0x40, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x7A, 0x01, 0x55, 0x54, 0x60, 0xDC, 0x85};
//unsigned char dataEEPROM_Samsung[24]           = {0x15, 0x02, 0x01, 0x02, 0x00, 0xF5, 0x0F, 0xA0, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x2E, 0x02, 0x44, 0x2A, 0x30, 0xD4, 0xD6};
//unsigned char dataEEPROM_XP3[24]               = {0x0D, 0x01, 0x03, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x54, 0x60, 0x0E, 0xDB};
//unsigned char dataEEPROM_XP5s[24]              = {0x0C, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x55, 0x54, 0x60, 0x52, 0xEA};
//unsigned char dataEEPROM_XP8[24]               = {0x0B, 0x01, 0x01, 0x02, 0x00, 0xF5, 0x0F, 0xA0, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5A, 0x01, 0x55, 0xA8, 0xC0, 0xCC, 0x13};
//unsigned char dataEEPROM_New_XP5[24]           = {0x0E, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x99, 0x54, 0x60, 0xDA, 0xCC};
//unsigned char dataEEPROM_Samsung_XCP[24]       = {0x16, 0x02, 0x02, 0x02, 0x00, 0xF5, 0x0F, 0xD2, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x49, 0x01, 0xBB, 0x2A, 0x30, 0xA2, 0x92};


#define CHARGER_ID_X5PLUS               1
#define CHARGER_ID_XP10                 2
#define CHARGER_ID_Orion_ros            3
#define CHARGER_ID_Samsung              4
#define CHARGER_ID_XP3                  5
#define CHARGER_ID_XP5s                 6
#define CHARGER_ID_XP8                  7
#define CHARGER_ID_New_XP5              8
#define CHARGER_ID_Samsung_XCP          9

#define CHARGER_ID_CP185_CP100D         10
#define CHARGER_ID_XP3300TRBO           11
#define CHARGER_ID_CP200                12
#define CHARGER_ID_KW_NX1000            13
#define CHARGER_ID_GO                   14

unsigned char dataEEPROM_ID[CHARGER_ID_GO][24] = {
  {0x0E, 0x01, 0x02, 0xFF, 0x00, 0xF5, 0x0D, 0xAC, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x55, 0x54, 0x60, 0x8A, 0x02},
  {0x0F, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x13, 0x88, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x7A, 0x01, 0x62, 0x54, 0x60, 0x96, 0x4C},
  {0x1F, 0x03, 0x01, 0x02, 0x00, 0xF5, 0x06, 0x40, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x7A, 0x01, 0x55, 0x54, 0x60, 0xDC, 0x85},
  {0x15, 0x02, 0x01, 0x02, 0x00, 0xF5, 0x0F, 0xA0, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x2E, 0x02, 0x44, 0x2A, 0x30, 0xD4, 0xD6},
  {0x0D, 0x01, 0x03, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x54, 0x60, 0x0E, 0xDB},
  {0x0C, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x55, 0x54, 0x60, 0x52, 0xEA},
  {0x0B, 0x01, 0x01, 0x02, 0x00, 0xF5, 0x0F, 0xA0, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5A, 0x01, 0x55, 0xA8, 0xC0, 0xCC, 0x13},
  {0x0E, 0x01, 0x02, 0x02, 0x00, 0xF5, 0x07, 0x08, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x5C, 0x01, 0x99, 0x54, 0x60, 0xDA, 0xCC},
  {0x16, 0x02, 0x02, 0x02, 0x00, 0xF5, 0x0F, 0xD2, 0x01, 0x1E, 0x00, 0xBF, 0x00, 0x99, 0x01, 0x33, 0x00, 0x49, 0x01, 0xBB, 0x2A, 0x30, 0xA2, 0x92},
  
  {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
  //{0x3F, 0x06, 0x03, 0x01, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x58, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0x7C, 0xA7},
  {0x3E, 0x06, 0x02, 0x01, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x3D, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0x13, 0xBA},
  {0x3D, 0x06, 0x01, 0x00, 0x01, 0xEB, 0x09, 0xC4, 0x02, 0x66, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x46, 0x50, 0xAD, 0x83},
  {0x47, 0x07, 0x01, 0x01, 0x01, 0xEB, 0x07, 0xD0, 0x02, 0x58, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x38, 0x40, 0xA3, 0x44},
  {0x29, 0x04, 0x01, 0x01, 0x01, 0xEB, 0x07, 0xD0, 0x02, 0x3D, 0x01, 0x7E, 0x00, 0x99, 0x01, 0x33, 0x00, 0x4C, 0x01, 0x55, 0x54, 0x60, 0x0D, 0x89},
};

unsigned char dataEEPROM[32];  

void CheckEEPROM(void)
{
  unsigned char addrEEPROM;  
  
  unsigned char countCheckMobile = 0;
  unsigned char countCheckDefault = 0;
  unsigned char countCheckDisconnect = 0;
  
  pwmDuty = PWM_DUTY_NONE;
  
  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
  {
    dataEEPROM[addrEEPROM] = SPI_Read(addrEEPROM);
  }
  for (int i = 1; i <= CHARGER_ID_GO; i++) {
    for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
    {
      if (dataEEPROM[addrEEPROM] != dataEEPROM_ID[i - 1][addrEEPROM]) break;
    }
    if (addrEEPROM == 24) { 
      isMobileSubBoard = i; 
//      putstring("Mobile XP5Plus SubBoard is connected\n"); 
      if (isMobileSubBoard <= CHARGER_ID_Samsung_XCP)
        pwmDuty = PWM_DUTY_MOBILE; 
      else
        pwmDuty = PWM_DUTY_RADIO; 
	  pwmDuty = PWM_DUTY_MOBILE;
      return; 
    }
	pwmDuty = PWM_DUTY_MOBILE;
  }
  
//  // Type of XP5 Plus
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP5Plus[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_X5PLUS; 
//    putstring("Mobile XP5Plus SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of XP10 
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP10[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_XP10; 
//    putstring("Mobile XP10 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of Orion ROS
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_Orion_ros[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_Orion_ros; 
//    putstring("Mobile ORION-ROS SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of Samsung
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_Samsung[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_Samsung; 
//    putstring("Mobile Samsung SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of XP3
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP3[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_XP3; 
//    putstring("Mobile XP3 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of XP5s
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP5s[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_XP5s; 
//    putstring("Mobile XP5 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of XP8
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP8[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_XP8; 
//    putstring("Mobile XP8 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of New XP5
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_New_XP5[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_New_XP5; 
//    putstring("Mobile New XP5 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of Samsung XCP
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_Samsung_XCP[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_Samsung_XCP; 
//    putstring("Mobile Samsung XCP SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_MOBILE; 
//    return; 
//  }
//  // Type of CP185_CP100D
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_CP185_CP100D[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_CP185_CP100D; 
//    putstring("Radio CP185_CP100D SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_RADIO; 
//    return; 
//  }
//  // Type of XP3300TRBO
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_XP3300TRBO[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_XP3300TRBO; 
//    putstring("Radio XP3300TRBO SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_RADIO; 
//    return; 
//  }
//  // Type of CP200
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_CP200[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_CP200; 
//    putstring("Radio CP200 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_RADIO; 
//    return; 
//  }
//  // Type of KW_NX1000
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_KW_NX1000[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_KW_NX1000; 
//    putstring("Radio KW_NX1000 SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_RADIO; 
//    return; 
//  }
//  // Type of GO!
//  for (addrEEPROM = 0; addrEEPROM < 24; addrEEPROM++)
//  {
//    if (dataEEPROM[addrEEPROM] != dataEEPROM_GO[addrEEPROM]) break;
//  }
//  if (addrEEPROM == 24) { 
//    isMobileSubBoard = CHARGER_ID_GO; 
//    putstring("Radio GO SubBoard is connected\n"); 
//    pwmDuty = PWM_DUTY_RADIO; 
//    return; 
//  }
}

uint8_t GetMobileSubBoardType(void)
{
    //if (isMobileSubBoard >= CHARGER_ID_X5PLUS && isMobileSubBoard <= CHARGER_ID_Samsung_XCP)
        return 1;
    //else if (isMobileSubBoard >= CHARGER_ID_CP185_CP100D && isMobileSubBoard <= CHARGER_ID_GO)
        //return 2;
    //else
        //return 0;
}

void SetPwmStop(void)
{
  pwmDuty = PWM_DUTY_NONE;
  TIM1_SetCompare3(pwmDuty);                              
  GPIO_WriteLow(GPIOA, GPIO_PIN_1); // FD2204 Disable
}

void SetPwmDuty(uint8_t duty)
{
  pwmDuty = duty;
  TIM1_SetCompare3(pwmDuty);
  GPIO_WriteHigh(GPIOA, GPIO_PIN_1); // FD2204 Enable   
}

void main( void )
{      
    unsigned int repeatCount_1_2 = 0;
    unsigned int repeatCount_1_3 = 0;
    unsigned int repeatCount_1_3_2 = 0;
    unsigned int repeatCount_2_1 = 0;
    unsigned int repeatCount_2_3 = 0;
    unsigned int repeatCount_3_1 = 0;
    unsigned int repeatCount_3_2 = 0;
    
    disableInterrupts();
    CLK_Config();
    
    GPIO_DeInit(GPIOA); 
    GPIO_DeInit(GPIOB); 
    GPIO_DeInit(GPIOC);
    GPIO_DeInit(GPIOD);
    
    // PIN1 : PD4
    GPIO_Init(GPIOD, GPIO_PIN_4, GPIO_MODE_OUT_PP_LOW_FAST);              // PD4 : NTC PULL DOWN
    GPIO_WriteLow(GPIOD, GPIO_PIN_4);  
    
    // Discharging control
    GPIO_Init(GPIOA, GPIO_PIN_3 , GPIO_MODE_OUT_PP_LOW_FAST);   // FLOAT for Discharging module
    GPIO_WriteLow(GPIOA, GPIO_PIN_3); // FLOAT Disable
    
    // Half-bridge control
    GPIO_Init(GPIOA, GPIO_PIN_1 , GPIO_MODE_OUT_PP_LOW_FAST);   // FD2204 ENABLE PIN
    GPIO_WriteLow(GPIOA, GPIO_PIN_1); // FD2204 Disable
    
    // PWM Control
    GPIO_Init(GPIOC, GPIO_PIN_3 , GPIO_MODE_OUT_PP_LOW_FAST);   // FD2204 PWM IN
    
    CLK_PeripheralClockConfig(CLK_PERIPHERAL_TIMER1, ENABLE);   
    
    // LED control
     
    GPIO_Init(GPIOB, GPIO_PIN_4 , GPIO_MODE_OUT_PP_LOW_FAST);   // Green LED PIN
    GPIO_Init(GPIOB, GPIO_PIN_5 , GPIO_MODE_OUT_PP_LOW_FAST);   // RED LED PIN
    GPIO_WriteHigh(GPIOB, GPIO_PIN_4);                          // Green LED OFF
    GPIO_WriteHigh(GPIOB, GPIO_PIN_5);                          // RED   LED OFF
    
    delay_ms(200);
    GPIO_WriteLow(GPIOB, GPIO_PIN_5);                          // RED   LED ON
    delay_ms(1000);
//    GPIO_WriteHigh(GPIOB, GPIO_PIN_5);                          // RED   LED OFF
    
    SPI_Config();
    UART_Config();        //PD5 UART TX
        
    CheckEEPROM();
    
    TIM1_DeInit();    
    
    TIM1_TimeBaseInit(CLK_PRESCALER_HSIDIV1, TIM1_COUNTERMODE_UP, (16000 / 50), 0);
    TIM1_OC3Init(TIM1_OCMODE_PWM1, TIM1_OUTPUTSTATE_ENABLE, TIM1_OUTPUTNSTATE_DISABLE,  pwmDuty, TIM1_OCPOLARITY_HIGH, TIM1_OCNPOLARITY_HIGH, TIM1_OCIDLESTATE_SET, TIM1_OCNIDLESTATE_RESET);
    TIM1_OC3PreloadConfig(ENABLE);  
    
    TIM1_Cmd(ENABLE);
    
    TIM1_CtrlPWMOutputs(ENABLE);
    TIM1_ARRPreloadConfig(ENABLE);
    
    if (GetMobileSubBoardType() != 2)
      GPIO_WriteHigh(GPIOA, GPIO_PIN_1); // FD2204 Enable  
    
    TIM2_DeInit();        
    TIM2_TimeBaseInit(CLK_PRESCALER_HSIDIV1, (16000 / 5));    
    TIM2_ITConfig(TIM2_IT_UPDATE,ENABLE);
    TIM2_Cmd(ENABLE);          

    ADC_Config();         //PD2 AIN3 VBat    PD3 AIN4 Isense
    ADC1_Cmd(ENABLE);                           //Turn on ADC1 the value will be store in ADC1_IRQHandler
    
    enableInterrupts();
  /* Infinite loop */
    while (1)
    {              
        if (timer2Flag)         //5KHz Loop
        {            
            timer2Flag = 0; 
            timer2Count++;
            
            ADC1_StartConversion();
            while(!ADC1_GetFlagStatus(ADC1_FLAG_EOC));
            ADC1_ClearFlag(ADC1_FLAG_EOC);
            
            AddBufferTemp(ADC1_GetBufferValue(2));
            AddBufferVoltage(ADC1_GetBufferValue(3));
            AddBufferCurrent(ADC1_GetBufferValue(4));
            
            switch(systemState)
            {
                case FULL_CHARGED:
                    
                    GPIO_WriteLow(GPIOB, GPIO_PIN_4);                           // Green LED ON
                    GPIO_WriteHigh(GPIOB, GPIO_PIN_5);                          // RED   LED OFF
                    
                    if ((GetMobileSubBoardType() == 1 && valISense > 160) || (GetMobileSubBoardType() == 2 && valISense > 220))
                    {   
                        repeatCount_1_2++;
                        if (repeatCount_1_2 > COUNT_LIMIT) {
                            repeatCount_1_2 = 0;
                            systemState = CHARGING;
                        }
                    }
                    else if ((GetMobileSubBoardType() == 1 && valISense < 4) || (GetMobileSubBoardType() == 2 && valISense < 6 && valTemp < 4) || (GetMobileSubBoardType() == 0))
                    {
                        if (repeatCount_1_3_2 == 0 && GetMobileSubBoardType() == 2 && pwmDuty == PWM_DUTY_RADIO)
                        {
                            SetPwmStop();
                        }
                        
                        repeatCount_1_3_2++;
                        repeatCount_1_3++;
                        if (GetMobileSubBoardType() != 2 && repeatCount_1_3 > COUNT_LIMIT) {
                            repeatCount_1_3 = 0;
                            if (GetMobileSubBoardType() != 2)
                            {
                              systemState = DISCONNECTED;
                            }
                        }
                        
                        if (GetMobileSubBoardType() == 2 && repeatCount_1_3_2 > 5000) {
                            repeatCount_1_3_2 = 0;
                            if (valVBat > 800)
                            {
                                systemState = DISCONNECTED;
                            }
                            else if (valVBat < 750)
                            {
                                if (pwmDuty == PWM_DUTY_NONE)
                                {
                                    SetPwmDuty(PWM_DUTY_RADIO);
                                }
                            }
                        }
                    }
                    else
                    {
                        if (GetMobileSubBoardType() == 2 && pwmDuty == PWM_DUTY_NONE)
                        {
                            SetPwmDuty(PWM_DUTY_RADIO);
                        }
                        repeatCount_1_2 = 0;
                        repeatCount_1_3 = 0;
                        repeatCount_1_3_2 = 0;
                    }
                    break;
                    
                case CHARGING:
                    GPIO_WriteHigh(GPIOB, GPIO_PIN_4);                          // Green LED OFF
                    GPIO_WriteLow(GPIOB, GPIO_PIN_5);                           // RED   LED ON    
                    
                    if ((GetMobileSubBoardType() == 1 && valVBat > 370 && valISense >= 40 && valISense < 120) || (GetMobileSubBoardType() == 2 && valISense > 20 && valISense < 60 && valTemp > 500))
                    {
                         repeatCount_2_1++;
                        if (repeatCount_2_1 > COUNT_LIMIT_FULLCHARGED) {
                            repeatCount_2_1 = 0;
                          systemState = FULL_CHARGED;
                        }
                    }
                    else if ((GetMobileSubBoardType() == 1 && valISense < 4) || (GetMobileSubBoardType() == 2 && valISense < 6 && valTemp < 4) || (GetMobileSubBoardType() == 0))
                    {
                        
                        repeatCount_2_3++;
                        if (repeatCount_2_3 > COUNT_LIMIT) {
                            repeatCount_2_3 = 0;
                            systemState = DISCONNECTED;                            
                            
                            if (GetMobileSubBoardType() == 2 && pwmDuty == PWM_DUTY_RADIO)
                            {
                                SetPwmStop();
                            }
                        }
                    }
                    else
                    {
                        if (GetMobileSubBoardType() == 2 && pwmDuty == PWM_DUTY_NONE)
                        {
                            SetPwmDuty(PWM_DUTY_RADIO);
                        }
                        
                        repeatCount_2_1 = 0;
                        repeatCount_2_3 = 0;
                    }
                    break;
                    
                case DISCONNECTED:
                    GPIO_WriteHigh(GPIOB, GPIO_PIN_4);                          // Green LED OFF
                    GPIO_WriteHigh(GPIOB, GPIO_PIN_5);                          // RED   LED OFF
                                        
                    if ((GetMobileSubBoardType() == 1 && valISense > 20) || (GetMobileSubBoardType() == 2 && valVBat < 573))
                    {            
                        repeatCount_3_1++;
                        if (repeatCount_3_1 > COUNT_LIMIT) {
                            repeatCount_3_1 = 0;
                            systemState = CHARGING;
                            
                            if (GetMobileSubBoardType() == 1 /* && pwmDuty == PWM_DUTY_NONE*/)
                            {                              
                                SetPwmDuty(PWM_DUTY_MOBILE);
                            }
                            else if (GetMobileSubBoardType() == 2 /*&& pwmDuty == PWM_DUTY_NONE*/)
                            {
                                SetPwmDuty(PWM_DUTY_RADIO);
                            } 
                        }
                    }
                    else if ((GetMobileSubBoardType() == 1 &&valISense < 21 && valISense > 4) || (GetMobileSubBoardType() == 2 && valVBat < 650))
                    {
                        repeatCount_3_2++;
                        if (repeatCount_3_2 > 5000) {
                            repeatCount_3_2 = 0;
                            systemState = FULL_CHARGED;    
                            
                            if (GetMobileSubBoardType() == 1 /* && pwmDuty == PWM_DUTY_NONE*/)
                            {                              
                                SetPwmDuty(PWM_DUTY_MOBILE);
                            }
                            else if (GetMobileSubBoardType() == 2/* && pwmDuty == PWM_DUTY_NONE*/)
                            {
                                SetPwmDuty(PWM_DUTY_RADIO);
                            } 
                        }
                    }
                    else
                    {
                        repeatCount_3_1 = 0;
                        repeatCount_3_2 = 0;
                    }
                    break;
                    
                default:
//                    GPIO_WriteHigh(GPIOB, GPIO_PIN_4);                          // Green LED OFF
//                    GPIO_WriteHigh(GPIOB, GPIO_PIN_5);                          // RED   LED OFF
//                    
//                    if ((GetMobileSubBoardType() == 1 && valISense > 160))
//                    {
//                        repeatCount_3_1++;
//                        if (repeatCount_3_1 > COUNT_LIMIT) {
//                            repeatCount_3_1 = 0;
//                            systemState = CHARGING;                            
//                        }
//                    }
//                    else if ((GetMobileSubBoardType() == 1 && valISense > 4)  || (GetMobileSubBoardType() == 2 && valVBat < 650))
//                    {
//                        repeatCount_3_2++;
//                        if (repeatCount_3_2 > COUNT_LIMIT) {
//                            repeatCount_3_2 = 0;
//                            systemState = FULL_CHARGED;       
//                                                        
//                            if (GetMobileSubBoardType() == 2 && pwmDuty == PWM_DUTY_NONE)
//                            {
//                                pwmDuty = PWM_DUTY_RADIO;
//                                TIM1_SetCompare3(pwmDuty);
//                                GPIO_WriteHigh(GPIOA, GPIO_PIN_1); // FD2204 Enable  
//                            }
//                        }
//                    }
//                    else
//                    {
//                        repeatCount_3_1 = 0;
//                        repeatCount_3_2 = 0;
//                    }
                    break;
            }
         
            if (timer2Count >= 5000)
            {          
              for (int i = 0; i < 24; i++)
              {
                UartSendInt(dataEEPROM[i]); putstring(" ");
              }
                putstring("\nD ");   UartSendInt(pwmDuty);
                putstring("\nS ");   UartSendInt(systemState);
                putstring("\nT ");   UartSendInt(valTemp);
                putstring("\nV ");   UartSendInt(valVBat);
                putstring("\nI "); UartSendInt(valISense);
                putstring("\n"); UartSendInt(GetMobileSubBoardType());
                timer2Count = 0;
            }
        }
    }

}


#ifdef USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *   where the assert_param error has occurred.
  * @param file: pointer to the source file name
  * @param line: assert_param error line source number
  * @retval : None
  */
void assert_failed(u8* file, u32 line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif