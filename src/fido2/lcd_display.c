/*
 * This file is part of the Pico FIDO2 distribution (https://github.com/polhenarejos/pico-fido2).
 * Copyright (c) 2025 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DEV_Config.h"
#include "LCD_1in47.h"
#include <stdio.h>

// RGB565颜色合成
static UWORD rgb_to_565(UBYTE r, UBYTE g, UBYTE b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// 彩虹渐变：赤橙黄绿青蓝紫
static void lcd_display_gradient(void) {
    UWORD j, i;
    UWORD line_buffer[64];
    UWORD width = LCD_1IN47.WIDTH;
    UWORD height = LCD_1IN47.HEIGHT;

    printf("Drawing rainbow gradient...\n");

    LCD_1IN47_SetWindows(0, 0, width, height);
    DEV_Digital_Write(LCD_DC_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 0);

    // 逐行绘制渐变
    for (j = 0; j < height; j++) {
        UWORD remaining = width;
        UWORD x_pos = 0;
        
        while (remaining > 0) {
            UWORD send_count = (remaining > 64) ? 64 : remaining;
            
            // 填充当前块的缓冲区
            for (i = 0; i < send_count; i++) {
                float ratio = (float)(x_pos + i) / (float)width;
                UBYTE r, g, b;
                
                // 彩虹6段：红→橙→黄→绿→青→蓝→紫
                if (ratio < 1.0f / 6.0f) {
                    // 红 → 橙
                    float t = ratio * 6.0f;
                    r = 255;
                    g = (UBYTE)(t * 165);
                    b = 0;
                } else if (ratio < 2.0f / 6.0f) {
                    // 橙 → 黄
                    float t = (ratio - 1.0f / 6.0f) * 6.0f;
                    r = 255;
                    g = 165 + (UBYTE)(t * (255 - 165));
                    b = 0;
                } else if (ratio < 3.0f / 6.0f) {
                    // 黄 → 绿
                    float t = (ratio - 2.0f / 6.0f) * 6.0f;
                    r = 255 - (UBYTE)(t * 255);
                    g = 255;
                    b = 0;
                } else if (ratio < 4.0f / 6.0f) {
                    // 绿 → 青
                    float t = (ratio - 3.0f / 6.0f) * 6.0f;
                    r = 0;
                    g = 255;
                    b = (UBYTE)(t * 255);
                } else if (ratio < 5.0f / 6.0f) {
                    // 青 → 蓝
                    float t = (ratio - 4.0f / 6.0f) * 6.0f;
                    r = 0;
                    g = 255 - (UBYTE)(t * 255);
                    b = 255;
                } else {
                    // 蓝 → 紫
                    float t = (ratio - 5.0f / 6.0f) * 6.0f;
                    r = (UBYTE)(t * 128);
                    g = 0;
                    b = 255;
                }

                UWORD color = rgb_to_565(r, g, b);
                // 转换字节序
                line_buffer[i] = ((color << 8) & 0xff00) | (color >> 8);
            }
            
            DEV_SPI_Write_nByte((uint8_t *)line_buffer, send_count * 2);
            remaining -= send_count;
            x_pos += send_count;
        }
    }

    DEV_Digital_Write(LCD_CS_PIN, 1);
    printf("Rainbow complete!\n");
}

int lcd_display_red(void) {
    printf("Initializing LCD display...\n");
    
    if (DEV_Module_Init() != 0) {
        printf("LCD module init failed!\n");
        return -1;
    }
    
    DEV_SET_PWM(100);
    LCD_1IN47_Init(VERTICAL);
    
    lcd_display_gradient();
    
    printf("LCD gradient display complete!\n");
    
    return 0;
}

