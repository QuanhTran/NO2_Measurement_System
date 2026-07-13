/******************************************************************************************************************
@File:  	CLCD I2C Chip PCF8574
@Author:  Khue Nguyen
@Website: khuenguyencreator.com
@Youtube: https://www.youtube.com/channel/UCt8cFnPOaHrQXWmVkk-lfvg
Huong dan su dung:
- Su dung thu vien HAL
- Khoi tao bien LCD: CLCD_I2C_Name LCD1;
- Khoi tao LCD do: CLCD_I2C_Init(&LCD1,&hi2c1,0x4e,20,4);
- Su dung cac ham phai truyen dia chi cua LCD do: 
CLCD_I2C_SetCursor(&LCD1, 0, 1);
CLCD_I2C_WriteString(&LCD1,"hello anh em ");
******************************************************************************************************************/
#include "lcd_i2c.h"

//************************** Low Level Function ****************************************************************//
static void CLCD_Delay(uint16_t Time)
{
	HAL_Delay(Time);
}
static void CLCD_WriteI2C(CLCD_I2C_Name* LCD, uint8_t Data, uint8_t Mode)
{
	char Data_H;
	char Data_L;
	uint8_t Data_I2C[4];
	Data_H = Data&0xF0;
	Data_L = (Data<<4)&0xF0;
	if(LCD->BACKLIGHT)
	{
		Data_H |= LCD_BACKLIGHT; 
		Data_L |= LCD_BACKLIGHT; 
	}
	if(Mode == CLCD_DATA)
	{
		Data_H |= LCD_RS;
		Data_L |= LCD_RS;
	}
	else if(Mode == CLCD_COMMAND)
	{
		Data_H &= ~LCD_RS;
		Data_L &= ~LCD_RS;
	}
	Data_I2C[0] = Data_H|LCD_EN;
	CLCD_Delay(1);
	Data_I2C[1] = Data_H;
	Data_I2C[2] = Data_L|LCD_EN;
	CLCD_Delay(1);
	Data_I2C[3] = Data_L;
	HAL_I2C_Master_Transmit(LCD->I2C, LCD->ADDRESS, (uint8_t *)Data_I2C, sizeof(Data_I2C), 1000);
}


//************************** High Level Function ****************************************************************//
void CLCD_I2C_Init(CLCD_I2C_Name* LCD, I2C_HandleTypeDef* hi2c_CLCD, uint8_t Address, uint8_t Colums, uint8_t Rows)
{
	LCD->I2C = hi2c_CLCD;
	LCD->ADDRESS = Address;
	LCD->COLUMS = Colums;
	LCD->ROWS = Rows;
	
	LCD->FUNCTIONSET = LCD_FUNCTIONSET|LCD_4BITMODE|LCD_2LINE|LCD_5x8DOTS;
	LCD->ENTRYMODE = LCD_ENTRYMODESET|LCD_ENTRYLEFT|LCD_ENTRYSHIFTDECREMENT;
	LCD->DISPLAYCTRL = LCD_DISPLAYCONTROL|LCD_DISPLAYON|LCD_CURSOROFF|LCD_BLINKOFF;
	LCD->CURSORSHIFT = LCD_CURSORSHIFT|LCD_CURSORMOVE|LCD_MOVERIGHT;
	LCD->BACKLIGHT = LCD_BACKLIGHT;

	CLCD_Delay(50);
	CLCD_WriteI2C(LCD, 0x33, CLCD_COMMAND);
//	CLCD_Delay(5);
	CLCD_WriteI2C(LCD, 0x33, CLCD_COMMAND);
	CLCD_Delay(5);
	CLCD_WriteI2C(LCD, 0x32, CLCD_COMMAND);
	CLCD_Delay(5);
	CLCD_WriteI2C(LCD, 0x20, CLCD_COMMAND);
	CLCD_Delay(5);
	
	CLCD_WriteI2C(LCD, LCD->ENTRYMODE,CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL,CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->CURSORSHIFT,CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->FUNCTIONSET,CLCD_COMMAND);
	
	CLCD_WriteI2C(LCD, LCD_CLEARDISPLAY,CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD_RETURNHOME,CLCD_COMMAND);
}
void CLCD_I2C_SetCursor(CLCD_I2C_Name* LCD, uint8_t Xpos, uint8_t Ypos)
{
	uint8_t DRAM_ADDRESS = 0x00;
	if(Xpos >= LCD->COLUMS)
	{
		Xpos = LCD->COLUMS - 1;
	}
	if(Ypos >= LCD->ROWS)
	{
		Ypos = LCD->ROWS -1;
	}
	if(Ypos == 0)
	{
		DRAM_ADDRESS = 0x00 + Xpos;
	}
	else if(Ypos == 1)
	{
		DRAM_ADDRESS = 0x40 + Xpos;
	}
	else if(Ypos == 2)
	{
		DRAM_ADDRESS = 0x14 + Xpos;
	}
	else if(Ypos == 3)
	{
		DRAM_ADDRESS = 0x54 + Xpos;
	}
	CLCD_WriteI2C(LCD, LCD_SETDDRAMADDR|DRAM_ADDRESS, CLCD_COMMAND);
}
void CLCD_I2C_WriteChar(CLCD_I2C_Name* LCD, char character)
{
	CLCD_WriteI2C(LCD, character, CLCD_DATA);
}
void CLCD_I2C_WriteString(CLCD_I2C_Name* LCD,const char *String)
{
	while(*String)CLCD_I2C_WriteChar(LCD, *String++);
}
void CLCD_I2C_Clear(CLCD_I2C_Name* LCD)
{
	CLCD_WriteI2C(LCD, LCD_CLEARDISPLAY, CLCD_COMMAND);
	CLCD_Delay(5);
}
void CLCD_I2C_ReturnHome(CLCD_I2C_Name* LCD)
{
	CLCD_WriteI2C(LCD, LCD_RETURNHOME, CLCD_COMMAND);
	CLCD_Delay(5);
}
void CLCD_I2C_CursorOn(CLCD_I2C_Name* LCD)
{
	LCD->DISPLAYCTRL |= LCD_CURSORON;
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL, CLCD_COMMAND);
}
void CLCD_I2C_CursorOff(CLCD_I2C_Name* LCD)
{
	LCD->DISPLAYCTRL &= ~LCD_CURSORON;
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL, CLCD_COMMAND);
}
void CLCD_I2C_BlinkOn(CLCD_I2C_Name* LCD)
{
	LCD->DISPLAYCTRL |= LCD_BLINKON;
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL, CLCD_COMMAND);
}
void CLCD_I2C_BlinkOff(CLCD_I2C_Name* LCD)
{
	LCD->DISPLAYCTRL &= ~LCD_BLINKON;
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL, CLCD_COMMAND);
}

/**
 * @brief  Khôi phục LCD từ trạng thái treo (dải đen dòng trên).
 *         Thực hiện: 1) Recovery bus I2C bằng bit-bang SCL
 *                    2) HAL DeInit + ReInit I2C
 *                    3) Khởi tạo lại toàn bộ LCD
 * @param  LCD: con trỏ tới cấu trúc LCD cần reset
 */
void CLCD_I2C_ForceReset(CLCD_I2C_Name* LCD)
{
	/* === BƯỚC 1: I2C Bus Recovery bằng bit-bang SCL ===
	 * Nếu slave (PCF8574) giữ SDA = LOW, ta toggle SCL 9 lần
	 * để slave nhả SDA, sau đó tạo điều kiện STOP. */
	I2C_HandleTypeDef *hi2c = LCD->I2C;
	I2C_TypeDef *instance = hi2c->Instance;

	/* Tạm tắt I2C peripheral */
	HAL_I2C_DeInit(hi2c);

	/* Xác định chân SCL/SDA theo Instance */
	GPIO_TypeDef *scl_port, *sda_port;
	uint16_t scl_pin, sda_pin;

	if (instance == I2C1) {
		scl_port = GPIOB; scl_pin = GPIO_PIN_6;  /* PB6 = I2C1_SCL */
		sda_port = GPIOB; sda_pin = GPIO_PIN_7;  /* PB7 = I2C1_SDA */
	} else if (instance == I2C2) {
		scl_port = GPIOB; scl_pin = GPIO_PIN_10; /* PB10 = I2C2_SCL */
		sda_port = GPIOB; sda_pin = GPIO_PIN_11; /* PB11 = I2C2_SDA */
	} else {
		/* Nếu không biết Instance → bỏ qua recovery, chỉ re-init */
		HAL_I2C_Init(hi2c);
		goto lcd_reinit;
	}

	/* Chuyển SCL và SDA thành GPIO Output Open-Drain để bit-bang */
	GPIO_InitTypeDef gpio;
	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;

	gpio.Pin = scl_pin;
	HAL_GPIO_Init(scl_port, &gpio);

	gpio.Pin = sda_pin;
	HAL_GPIO_Init(sda_port, &gpio);

	/* Đảm bảo SDA = HIGH trước khi bắt đầu */
	HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_SET);

	/* Toggle SCL 9 lần để slave nhả SDA */
	for (int i = 0; i < 9; i++) {
		HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_RESET);
		CLCD_Delay(1);
		HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);
		CLCD_Delay(1);

		/* Nếu SDA đã được nhả (HIGH) thì dừng sớm */
		if (HAL_GPIO_ReadPin(sda_port, sda_pin) == GPIO_PIN_SET) {
			break;
		}
	}

	/* Tạo điều kiện STOP: SDA LOW → HIGH khi SCL HIGH */
	HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_RESET);
	CLCD_Delay(1);
	HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_SET);
	CLCD_Delay(1);
	HAL_GPIO_WritePin(sda_port, sda_pin, GPIO_PIN_SET);
	CLCD_Delay(1);

	/* === BƯỚC 2: Khởi tạo lại I2C bằng HAL === */
	HAL_I2C_Init(hi2c);

lcd_reinit:
	/* === BƯỚC 3: Khởi tạo lại LCD từ đầu === */
	LCD->FUNCTIONSET = LCD_FUNCTIONSET|LCD_4BITMODE|LCD_2LINE|LCD_5x8DOTS;
	LCD->ENTRYMODE = LCD_ENTRYMODESET|LCD_ENTRYLEFT|LCD_ENTRYSHIFTDECREMENT;
	LCD->DISPLAYCTRL = LCD_DISPLAYCONTROL|LCD_DISPLAYON|LCD_CURSOROFF|LCD_BLINKOFF;
	LCD->CURSORSHIFT = LCD_CURSORSHIFT|LCD_CURSORMOVE|LCD_MOVERIGHT;
	LCD->BACKLIGHT = LCD_BACKLIGHT;

	CLCD_Delay(50);
	CLCD_WriteI2C(LCD, 0x33, CLCD_COMMAND);
	CLCD_WriteI2C(LCD, 0x33, CLCD_COMMAND);
	CLCD_Delay(5);
	CLCD_WriteI2C(LCD, 0x32, CLCD_COMMAND);
	CLCD_Delay(5);
	CLCD_WriteI2C(LCD, 0x20, CLCD_COMMAND);
	CLCD_Delay(5);

	CLCD_WriteI2C(LCD, LCD->ENTRYMODE, CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->DISPLAYCTRL, CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->CURSORSHIFT, CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD->FUNCTIONSET, CLCD_COMMAND);

	CLCD_WriteI2C(LCD, LCD_CLEARDISPLAY, CLCD_COMMAND);
	CLCD_WriteI2C(LCD, LCD_RETURNHOME, CLCD_COMMAND);
}
