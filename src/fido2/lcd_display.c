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

#define LINE_HEIGHT 24
#define NUM_LINES 3
static UBYTE *text_buffer = NULL;

static float rainbow_offset = 0.0f;
static float text_offset = 0.0f;
static struct repeating_timer lcd_timer;

static const char *scroll_texts[NUM_LINES] = {
    "  >>>>  Pico FIDO2  <<<<  ",
    "  Rainbow Scrolling  ",
    "  Three Lines Demo  "
};

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
}

static void set_pixel_in_buffer(UWORD x, UWORD y, UWORD color) {
    if (x >= LCD_1IN47.WIDTH || y >= LINE_HEIGHT * NUM_LINES) return;

    UDOUBLE Addr = x * 2 + y * LCD_1IN47.WIDTH * 2;
    text_buffer[Addr] = (UBYTE)(color >> 8);
    text_buffer[Addr + 1] = (UBYTE)(color & 0xFF);
}

static void draw_char_to_buffer(UWORD x, UWORD y, char ch, sFONT *font, UWORD color) {
    if (y >= LINE_HEIGHT * NUM_LINES || x >= LCD_1IN47.WIDTH) return;

    UWORD Page, Column;
    UWORD width = font->Width;
    UWORD height = font->Height;

    uint32_t Char_Offset = (ch - ' ') * height * (width / 8 + (width % 8 ? 1 : 0));
    const unsigned char *ptr = &font->table[Char_Offset];

    for (Page = 0; Page < height; Page++) {
        if (y + Page >= LINE_HEIGHT * NUM_LINES) break;

        for (Column = 0; Column < width; Column++) {
            if (x + Column >= LCD_1IN47.WIDTH) break;

            if (*ptr & (0x80 >> (Column % 8))) {
                set_pixel_in_buffer(x + Column, y + Page, color);
            }

            if (Column % 8 == 7) ptr++;
        }
        if (width % 8 != 0) ptr++;
    }
}

static void fill_text_buffer_black(void) {
    UWORD black = rgb_to_565(0, 0, 0);
    for (UWORD y = 0; y < LINE_HEIGHT * NUM_LINES; y++) {
        for (UWORD x = 0; x < LCD_1IN47.WIDTH; x++) {
            set_pixel_in_buffer(x, y, black);
        }
    }
}

static void draw_rainbow_text_line_to_buffer(int line_idx, sFONT *font) {
    const char *scroll_text = scroll_texts[line_idx];
    int text_len = strlen(scroll_text);
    int char_width = font->Width;

    int scroll_chars = (int)(text_offset / char_width);
    float char_fraction = (text_offset - scroll_chars * char_width) / (float)char_width;
    int start_x = -(int)(char_fraction * char_width);

    int text_y = line_idx * LINE_HEIGHT + (LINE_HEIGHT - font->Height) / 2;

    for (int i = 0; i < text_len + 2; i++) {
        int idx = (scroll_chars + i) % text_len;
        char ch = scroll_text[idx];

        int x_pos = start_x + i * char_width;
        if (x_pos > (int)LCD_1IN47.WIDTH) continue;
        if (x_pos + char_width < 0) continue;

        float color_ratio = (float)(scroll_chars + i) / (float)text_len;
        color_ratio += rainbow_offset;
        color_ratio += (float)line_idx * 0.33f;
        while (color_ratio > 1.0f) color_ratio -= 1.0f;

        UWORD color;
        get_rainbow_color(color_ratio, &color);

        draw_char_to_buffer((UWORD)x_pos, (UWORD)text_y, ch, font, color);
    }
}

static bool lcd_animation_callback(struct repeating_timer *t) {
    (void)t;

    fill_text_buffer_black();

    for (int i = 0; i < NUM_LINES; i++) {
        draw_rainbow_text_line_to_buffer(i, &Font24);
    }

    UWORD y_start = (LCD_1IN47.HEIGHT - LINE_HEIGHT * NUM_LINES) / 2;

    LCD_1IN47_SetWindows(0, y_start, LCD_1IN47.WIDTH, y_start + LINE_HEIGHT * NUM_LINES);
    DEV_Digital_Write(LCD_DC_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 0);
    DEV_SPI_Write_nByte(text_buffer, LCD_1IN47.WIDTH * LINE_HEIGHT * NUM_LINES * 2);
    DEV_Digital_Write(LCD_CS_PIN, 1);

    rainbow_offset += 0.005f;
    if (rainbow_offset > 1.0f) rainbow_offset = 0.0f;

    text_offset += 0.5f;
    if (text_offset >= Font24.Width * strlen(scroll_texts[0])) {
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

    LCD_1IN47_Clear(BLACK);

    text_buffer = (UBYTE *)malloc(LCD_1IN47.WIDTH * LINE_HEIGHT * NUM_LINES * 2);
    if (!text_buffer) {
        printf("ERROR: Failed to allocate text buffer!\n");
        return -1;
    }

    add_repeating_timer_ms(20, lcd_animation_callback, NULL, &lcd_timer);
    printf("LCD rainbow text animation started!\n");

    return 0;
}

#else /* ENABLE_LCD */

int lcd_display_red() {
    return 0;
}

#endif /* ENABLE_LCD */