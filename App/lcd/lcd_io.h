#ifndef LCD_IO_H
#define LCD_IO_H

#include <stdint.h>

void LCD_IO_Init(void);
void LCD_IO_WriteData(uint16_t reg_value);
void LCD_IO_WriteReg(uint8_t reg);
uint32_t LCD_IO_ReadData(uint16_t reg_value, uint8_t read_size);
void LCD_Delay(uint32_t delay);

#endif /* LCD_IO_H */
