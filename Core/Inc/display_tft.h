#ifndef INC_DISPLAY_TFT_H_
#define INC_DISPLAY_TFT_H_

#include <stdint.h>
#include "stm32f4xx_hal.h"
// === Public functions ===
void TFT_Init(SPI_HandleTypeDef *hspi);
void TFT_FillScreen(uint16_t color);
void TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bgcolor);
void TFT_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgcolor);

void TFT_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void TFT_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void TFT_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

void TFT_UpdateValues(float temp_bottom, float temp_middle, float temp_top, uint8_t heater_power);
void TFT_DrawDestilleryScreen(void);

// === Color definitions ===
#define BLACK       0x0000
#define NAVY        0x000F
#define DARKGREEN   0x03E0
#define DARKCYAN    0x03EF
#define MAROON      0x7800
#define PURPLE      0x780F
#define OLIVE       0x7BE0
#define LIGHTGREY   0xC618
#define DARKGREY    0x7BEF
#define BLUE        0x001F
#define GREEN       0x07E0
#define CYAN        0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define WHITE       0xFFFF
#define ORANGE      0xFD20
#define GREENYELLOW 0xAFE5
#define PINK        0xF81F
#define WHITE   	0xFFFF

#endif /* INC_DISPLAY_TFT_H_ */
