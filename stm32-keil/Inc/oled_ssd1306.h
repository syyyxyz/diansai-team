#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <stdint.h>

#define OLED_WIDTH  128U
#define OLED_HEIGHT  64U

uint8_t OLED_Init(void);
void OLED_Clear(void);
void OLED_Print(uint8_t column, uint8_t row, const char *text);
void OLED_PrintU32(uint8_t column, uint8_t row, uint32_t value);
void OLED_PrintU64(uint8_t column, uint8_t row, uint64_t value);
void OLED_PrintS64(uint8_t column, uint8_t row, int64_t value);
uint8_t OLED_Update(void);

#endif
