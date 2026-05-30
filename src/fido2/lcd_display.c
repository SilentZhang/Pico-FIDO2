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
#include "GUI_Paint.h"
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_LCD

#ifdef PICO_PLATFORM
#include "bsp/board.h"
#include "pico/time.h"
#endif

#define LINE_BUFFER_SIZE 64
static UWORD line_buffer[LINE_BUFFER_SIZE];

static float rainbow_offset = 0.0f;
static float text_offset = 0.0f;
static struct repeating_timer lcd_timer;

static const char scroll_text[] = "  >>>>  Pico FIDO2 Rainbow Text Scrolling Demo  <<<<  ";

static UWORD rgb_to_565(UBYTE r, UBYTE g, UBYTE b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void get_rainbow_color(float ratio, UWORD *color) {
    UBYTE r, g, b;

    if (ratio < 1.0f / 6.0f) {
        float t = ratio * 6.0f;
        r = 255;
        g = (UBYTE)(t * 165);
        b = 0;
    } else if (ratio < 2.0f / 6.0f) {
        float t = (ratio - 1.0f / 6.0f) * 6.0f;
        r = 255;
        g = 165 + (UBYTE)(t * (255 - 165));
        b = 0;
    } else if (ratio < 3.0f / 6.0f) {
        float t = (ratio - 2.0f / 6.0f) * 6.0f;
        r = 255 - (UBYTE)(t * 255);
        g = 255;
        b = 0;
    } else if (ratio < 4.0f / 6.0f) {
        float t = (ratio - 3.0f / 6.0f) * 6.0f;
        r = 0;
        g = 255;
        b = (UBYTE)(t * 255);
    } else if (ratio < 5.0f / 6.0f) {
        float t = (ratio - 4.0f / 6.0f) * 6.0f;
        r = 0;
        g = 255 - (UBYTE)(t * 255);
        b = 255;
    } else {
        float t = (ratio - 5.0f / 6.0f) * 6.0f;
        r = (UBYTE)(t * 128);
        g = 0;
        b = 255;
    }

    *color = rgb_to_565(r, g, b);
    *color = ((*color << 8) & 0xff00) | (*color >> 8);
}

static void draw_rainbow_background(void) {
    UWORD j, i;
    UWORD width = LCD_1IN47.WIDTH;
    UWORD height = LCD_1IN47.HEIGHT;

    LCD_1IN47_SetWindows(0, 0, width, height);
    DEV_Digital_Write(LCD_DC_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 0);

    for (j = 0; j < height; j++) {
        float ratio = (float)j / (float)height;
        ratio += rainbow_offset;
        while (ratio > 1.0f) ratio -= 1.0f;

        UWORD bg_color;
        get_rainbow_color(ratio, &bg_color);

        UWORD remaining = width;
        UWORD x_pos = 0;

        while (remaining > 0) {
            UWORD send_count = (remaining > LINE_BUFFER_SIZE) ? LINE_BUFFER_SIZE : remaining;

            for (i = 0; i < send_count; i++) {
                line_buffer[i] = bg_color;
            }

            DEV_SPI_Write_nByte((uint8_t *)line_buffer, send_count * 2);
            remaining -= send_count;
            x_pos += send_count;
        }
    }

    DEV_Digital_Write(LCD_CS_PIN, 1);
}

static void draw_char_pixel(UWORD x, UWORD y, UWORD color) {
    LCD_1IN47_DisplayPoint(x, y, color);
}

static void draw_char_with_color(UWORD x, UWORD y, char ch, sFONT *font, UWORD color) {
    UWORD Page, Column;
    UWORD width = font->Width;
    UWORD height = font->Height;

    uint32_t Char_Offset = (ch - ' ') * height * (width / 8 + (width % 8 ? 1 : 0));
    const unsigned char *ptr = &font->table[Char_Offset];

    for (Page = 0; Page < height; Page++) {
        for (Column = 0; Column < width; Column++) {
            if (*ptr & (0x80 >> (Column % 8))) {
                draw_char_pixel(x + Column, y + Page, color);
            }

            if (Column % 8 == 7)
                ptr++;
        }
        if (width % 8 != 0)
            ptr++;
    }
}

static void draw_rainbow_text(UWORD y_pos, sFONT *font) {
    int text_len = strlen(scroll_text);
    int char_width = font->Width;

    int scroll_chars = (int)(text_offset / char_width);
    float char_fraction = (text_offset - scroll_chars * char_width) / (float)char_width;

    int start_x = -(int)(char_fraction * char_width);

    for (int i = 0; i < text_len + 2; i++) {
        int idx = (scroll_chars + i) % text_len;
        char ch = scroll_text[idx];

        int x_pos = start_x + i * char_width;
        if (x_pos > (int)LCD_1IN47.WIDTH) continue;
        if (x_pos + char_width < 0) continue;

        float color_ratio = (float)(scroll_chars + i) / (float)text_len;
        color_ratio += rainbow_offset;
        while (color_ratio > 1.0f) color_ratio -= 1.0f;

        UWORD color;
        get_rainbow_color(color_ratio, &color);

        draw_char_with_color((UWORD)x_pos, y_pos, ch, font, color);
    }
}

static bool lcd_animation_callback(struct repeating_timer *t) {
    (void)t;

    draw_rainbow_background();

    UWORD text_y = (LCD_1IN47.HEIGHT - Font16.Height) / 2;
    draw_rainbow_text(text_y, &Font16);

    rainbow_offset += 0.002f;
    if (rainbow_offset > 1.0f) rainbow_offset = 0.0f;

    text_offset += 1.5f;
    if (text_offset > Font16.Width * strlen(scroll_text)) {
        text_offset = 0.0f;
    }

    return true;
}

int lcd_display_red() {
    printf("=== Starting LCD Rainbow Text Scroll ===\n");

    if (DEV_Module_Init() != 0) {
        printf("ERROR: DEV_Module_Init failed!\n");
        return -1;
    }

    DEV_SET_PWM(90);
    LCD_1IN47_Init(VERTICAL);

    add_repeating_timer_ms(30, lcd_animation_callback, NULL, &lcd_timer);
    printf("LCD rainbow text animation started!\n");

    return 0;
}

#else /* ENABLE_LCD */

int lcd_display_red() {
    return 0;
}

#endif /* ENABLE_LCD */