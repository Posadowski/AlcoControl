#include "display_tft.h"
#include "fonts.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#define TFT_WIDTH   128
#define TFT_HEIGHT  160

// === Private defines ===
#define DC_PIN      GPIO_PIN_0
#define DC_PORT     GPIOA

#define RESET_PIN   GPIO_PIN_1
#define RESET_PORT  GPIOA

#define CS_PIN      GPIO_PIN_4
#define CS_PORT     GPIOA

// === Private variables ===
static SPI_HandleTypeDef *tft_hspi;

// === Private functions ===
static void TFT_WriteCommand(uint8_t cmd);
static void TFT_WriteData(uint8_t *buff, size_t buff_size);
static void TFT_WriteDataByte(uint8_t data);
static void TFT_WriteData16(uint16_t data);

static uint16_t RGBtoBGR(uint16_t color) {
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    return (b << 11) | (g << 5) | r;
}


// === Implementations ===
void TFT_Init(SPI_HandleTypeDef *hspi) {
    tft_hspi = hspi;

    // Hardware reset
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(50);

    // Initialization sequence for ST7735
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);

    TFT_WriteCommand(0x01); // Software reset
    HAL_Delay(150);

    TFT_WriteCommand(0x11); // Sleep out
    HAL_Delay(150);

    TFT_WriteCommand(0x3A); // Color mode
    TFT_WriteDataByte(0x05); // 16-bit color

    TFT_WriteCommand(0x36); // Memory Data Access Control
    TFT_WriteDataByte(0xC8);

    TFT_WriteCommand(0x29); // Display ON
    HAL_Delay(100);

    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

void TFT_FillScreen(uint16_t color) {
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    TFT_WriteCommand(0x2A); // Column addr set
    uint8_t data_col[] = {0x00, 0x00, 0x00, TFT_WIDTH - 1};
    TFT_WriteData(data_col, sizeof(data_col));

    TFT_WriteCommand(0x2B); // Row address set
    uint8_t data_row[] = {0x00, 0x00, 0x00, TFT_HEIGHT - 1};
    TFT_WriteData(data_row, sizeof(data_row));

    TFT_WriteCommand(0x2C); // Memory write

    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);

    for (uint16_t i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) {
        uint8_t data[2] = {hi, lo};
        HAL_SPI_Transmit(tft_hspi, data, 2, HAL_MAX_DELAY);
    }

    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

void TFT_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    color = RGBtoBGR(color);
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    TFT_WriteCommand(0x2A); // Column address set
    uint8_t data_col[] = {0x00, x, 0x00, x};
    TFT_WriteData(data_col, sizeof(data_col));

    TFT_WriteCommand(0x2B); // Row address set
    uint8_t data_row[] = {0x00, y, 0x00, y};
    TFT_WriteData(data_row, sizeof(data_row));

    TFT_WriteCommand(0x2C); // Memory write

    uint8_t data[2] = {hi, lo};
    TFT_WriteData(data, 2);
}

void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bgcolor) {
    if ((x >= TFT_WIDTH) || (y >= TFT_HEIGHT)) return;

    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = font5x7[(c - 32) * 5 + i];
        for (uint8_t j = 0; j < 7; j++) {
            if (line & 0x1) {
                TFT_DrawPixel(x + i, y + j, color);
            } else {
                TFT_DrawPixel(x + i, y + j, bgcolor);
            }
            line >>= 1;
        }
    }
}

void TFT_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgcolor) {
    while (*str) {
        TFT_DrawChar(x, y, *str++, color, bgcolor);
        x += 6;
        if (x + 5 >= TFT_WIDTH) {
            x = 0;
            y += 8;
        }
    }
}

void TFT_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if ((w <= 0) || (h <= 0)) return;
    color = RGBtoBGR(color);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);

    TFT_WriteCommand(0x2A); // Column address set
    TFT_WriteDataByte(0x00);
    TFT_WriteDataByte(x);
    TFT_WriteDataByte(0x00);
    TFT_WriteDataByte(x + w - 1);

    TFT_WriteCommand(0x2B); // Row address set
    TFT_WriteDataByte(0x00);
    TFT_WriteDataByte(y);
    TFT_WriteDataByte(0x00);
    TFT_WriteDataByte(y + h - 1);

    TFT_WriteCommand(0x2C); // Memory write

    for (int32_t i = 0; i < w * h; i++) {
    	TFT_WriteData16(color);
    }

    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

void TFT_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    TFT_FillRect(x, y, w, 1, color);         // Top
    TFT_FillRect(x, y + h - 1, w, 1, color);  // Bottom
    TFT_FillRect(x, y, 1, h, color);          // Left
    TFT_FillRect(x + w - 1, y, 1, h, color);  // Right
}

void TFT_DrawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    TFT_DrawPixel(x0, y0 + r, color);
    TFT_DrawPixel(x0, y0 - r, color);
    TFT_DrawPixel(x0 + r, y0, color);
    TFT_DrawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        TFT_DrawPixel(x0 + x, y0 + y, color);
        TFT_DrawPixel(x0 - x, y0 + y, color);
        TFT_DrawPixel(x0 + x, y0 - y, color);
        TFT_DrawPixel(x0 - x, y0 - y, color);
        TFT_DrawPixel(x0 + y, y0 + x, color);
        TFT_DrawPixel(x0 - y, y0 + x, color);
        TFT_DrawPixel(x0 + y, y0 - x, color);
        TFT_DrawPixel(x0 - y, y0 - x, color);
    }
}

// === Low level helpers ===
static void TFT_WriteCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(tft_hspi, &cmd, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

static void TFT_WriteData(uint8_t *buff, size_t buff_size) {
    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(tft_hspi, buff, buff_size, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

static void TFT_WriteDataByte(uint8_t data) {
    TFT_WriteData(&data, 1);
}

static void TFT_WriteData16(uint16_t data)
{
    uint8_t buff[2];
    buff[0] = data >> 8;    // MSB
    buff[1] = data & 0xFF;  // LSB

    HAL_GPIO_WritePin(DC_PORT, DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_RESET);
    HAL_SPI_Transmit(tft_hspi, buff, 2, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, GPIO_PIN_SET);
}

void TFT_DrawDestilleryScreen(void) {
    TFT_FillScreen(BLACK);

    // Draw a simplified distillery:
    // Column
    TFT_DrawRect(50, 20, 30, 90, WHITE); // main column
    // Tank
    TFT_DrawRect(40, 110, 50, 30, WHITE); // mash tank
    // Coil
    TFT_DrawCircle(65, 10, 5, WHITE); // cooler on top

    // Signatures
    TFT_DrawString(5, 20, "Top", WHITE, BLACK);
    TFT_DrawString(5, 60, "Middle", WHITE, BLACK);
    TFT_DrawString(5, 120, "Bottom", WHITE, BLACK);

    TFT_DrawString(5, 150, "Heater:", WHITE, BLACK);
}

void TFT_UpdateValues(float temp_bottom, float temp_middle, float temp_top, float heater_power) {
    char buffer[10];

    // temperature
    if (temp_top > -100) {
		snprintf(buffer, sizeof(buffer), "%.1fC", temp_top);
		TFT_DrawString(90, 20, buffer, CYAN, BLACK);
    } else {
    	TFT_DrawString(90, 20, "N/A", RED, BLACK);
    }

    if (temp_middle > -100) {
		snprintf(buffer, sizeof(buffer), "%.1fC", temp_middle);
		TFT_DrawString(90, 60, buffer, CYAN, BLACK);
    } else {
		TFT_DrawString(90, 60, "N/A", RED, BLACK);
    }
    if(temp_bottom > -100){
    	snprintf(buffer, sizeof(buffer), "%.1fC", temp_bottom);
    	TFT_DrawString(90, 120, buffer, CYAN, BLACK);
    } else {
		TFT_DrawString(90, 120, "N/A", RED, BLACK);
    }
    // Heater
    TFT_FillRect(70, 150, 50, 10, BLACK); // clear screen only on power string position
    snprintf(buffer, sizeof(buffer), "%0.1f%%", heater_power);
    TFT_DrawString(70, 150, buffer, YELLOW, BLACK);

    // simple heater power strip
    TFT_FillRect(70, 142, 50, 5, BLACK); // clear screen on power strip position
    TFT_FillRect(70, 142, heater_power/2, 5, GREEN); // (max. strip 50px for 100%)
}
