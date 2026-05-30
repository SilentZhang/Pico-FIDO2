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
#include "board.h"
#include "pico_keys.h"
#include "files.h"
#include "asn1.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <string.h>

#ifdef ENABLE_LCD

#ifdef PICO_PLATFORM
#include "pico/time.h"
#endif

extern const mbedtls_md_info_t *get_oath_md_info(uint8_t alg);

#define LINE_HEIGHT 24
#define NUM_LINES 3
#define TOTP_PERIOD 30

#define TAG_NAME            0x71
#define TAG_KEY             0x73
#define OATH_TYPE_TOTP      0x20
#define OATH_TYPE_MASK      0xf0

static UBYTE *text_buffer = NULL;
static struct repeating_timer lcd_timer;
static char current_otp[12] = {0};
static int current_otp_len = 0;
static uint8_t current_otp_key[64] = {0};
static int current_otp_key_len = 0;
static uint32_t last_totp_time = 0;
static uint8_t oath_cred_idx = 0xFF;
static float rainbow_offset = 0.0f;
static float text_offset = 0.0f;
static char current_cred_name[64] = {0};

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

static int calculate_totp(const uint8_t *key, size_t key_len, uint64_t time_val, char *otp_out, int *otp_len_out) {
    uint8_t chal[8];
    put_uint64_t_be(time_val, chal);

    const mbedtls_md_info_t *md_info = get_oath_md_info(key[0]);
    if (md_info == NULL) {
        return -1;
    }

    uint8_t hmac[64];
    int r = mbedtls_md_hmac(md_info, key + 2, key_len - 2, chal, sizeof(chal), hmac);
    if (r != 0) {
        return -1;
    }

    size_t hmac_size = mbedtls_md_get_size(md_info);
    uint8_t offset = hmac[hmac_size - 1] & 0x0f;

    uint8_t res_buf[4];
    res_buf[0] = hmac[offset] & 0x7f;
    res_buf[1] = hmac[offset + 1];
    res_buf[2] = hmac[offset + 2];
    res_buf[3] = hmac[offset + 3];

    uint32_t otp_val = get_uint32_t_be(res_buf);

    int digits = (key[1] & 0x0f);
    if (digits == 0) digits = 6;
    if (digits > 8) digits = 8;

    if (digits == 8) {
        otp_val %= (uint32_t) 100000000;
        sprintf(otp_out, "%08lu", (unsigned long) otp_val);
    } else {
        otp_val %= (uint32_t) 1000000;
        sprintf(otp_out, "%06lu", (unsigned long) otp_val);
    }

    *otp_len_out = digits;
    return 0;
}

static void find_first_totp_credential(void) {
    oath_cred_idx = 0xFF;
    current_otp_key_len = 0;
    memset(current_cred_name, 0, sizeof(current_cred_name));

    for (int i = 0; i < 255; i++) {
        file_t *ef = search_dynamic_file((uint16_t)(0xBA00 + i));
        if (file_has_data(ef)) {
            const uint8_t *ef_data = file_get_data(ef);
            size_t ef_len = file_get_size(ef);

            asn1_ctx_t ctx;
            asn1_ctx_init((uint8_t *)ef_data, (uint16_t)ef_len, &ctx);

            asn1_ctx_t key_ctx, name_ctx;
            if (asn1_find_tag(&ctx, TAG_KEY, &key_ctx) == true) {
                if ((key_ctx.data[0] & OATH_TYPE_MASK) == OATH_TYPE_TOTP) {
                    oath_cred_idx = i;
                    current_otp_key_len = key_ctx.len;
                    if (current_otp_key_len > sizeof(current_otp_key)) {
                        current_otp_key_len = sizeof(current_otp_key);
                    }
                    memcpy(current_otp_key, key_ctx.data, current_otp_key_len);
                    
                    if (asn1_find_tag(&ctx, TAG_NAME, &name_ctx) == true) {
                        int name_len = name_ctx.len;
                        if (name_len > sizeof(current_cred_name) - 1) {
                            name_len = sizeof(current_cred_name) - 1;
                        }
                        memcpy(current_cred_name, name_ctx.data, name_len);
                        current_cred_name[name_len] = '\0';
                        
                        char *colon_pos = strchr(current_cred_name, ':');
                        if (colon_pos != NULL) {
                            *colon_pos = '\0';
                        } else {
                            char *at_pos = strchr(current_cred_name, '@');
                            if (at_pos != NULL) {
                                *at_pos = '\0';
                            }
                        }
                    }
                    return;
                }
            }
        }
    }
}

static void draw_scrolling_totp(void) {
    time_t now_sec = get_rtc_time();
    uint64_t time_val = (uint64_t) now_sec / TOTP_PERIOD;
    uint32_t remaining = TOTP_PERIOD - ((uint32_t) now_sec % TOTP_PERIOD);

    if (time_val != last_totp_time || current_otp_key_len == 0) {
        last_totp_time = (uint32_t) time_val;

        if (current_otp_key_len == 0) {
            find_first_totp_credential();
        }

        if (current_otp_key_len > 0) {
            calculate_totp(current_otp_key, current_otp_key_len, time_val, current_otp, &current_otp_len);
        } else {
            strcpy(current_otp, "No TOTP");
            current_otp_len = 7;
        }
    }

    fill_text_buffer_black();

    int char_width = Font24.Width;
    int scroll_chars = (int)(text_offset / char_width);
    float char_fraction = (text_offset - scroll_chars * char_width) / (float)char_width;
    int start_x = -(int)(char_fraction * char_width);

    int line1_y = LINE_HEIGHT;

    char scroll_text[100];
    if (current_otp_key_len > 0 && current_cred_name[0] != '\0') {
        char countdown[8];
        sprintf(countdown, "(%ds)", (unsigned int)remaining);
        sprintf(scroll_text, "  [%lld] %s: %s %s  ", (long long)now_sec, current_cred_name, current_otp, countdown);
    } else if (current_otp_key_len > 0) {
        char countdown[8];
        sprintf(countdown, "(%ds)", (unsigned int)remaining);
        sprintf(scroll_text, "  [%lld] %s %s  ", (long long)now_sec, current_otp, countdown);
    } else {
        sprintf(scroll_text, "  [%lld] %s  ", (long long)now_sec, current_otp);
    }

    int text_len = strlen(scroll_text);

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

        draw_char_to_buffer((UWORD)x_pos, (UWORD)line1_y, ch, &Font24, color);
    }

    rainbow_offset += 0.005f;
    if (rainbow_offset > 1.0f) rainbow_offset = 0.0f;

    text_offset += 2.0f;
    if (text_offset >= char_width * text_len) {
        text_offset = 0.0f;
    }
}

static void flush_buffer_to_lcd(void) {
    UWORD y_start = (LCD_1IN47.HEIGHT - LINE_HEIGHT * NUM_LINES) / 2;
    LCD_1IN47_SetWindows(0, y_start, LCD_1IN47.WIDTH, y_start + LINE_HEIGHT * NUM_LINES);
    DEV_Digital_Write(LCD_DC_PIN, 1);
    DEV_Digital_Write(LCD_CS_PIN, 0);
    DEV_SPI_Write_nByte(text_buffer, LCD_1IN47.WIDTH * LINE_HEIGHT * NUM_LINES * 2);
    DEV_Digital_Write(LCD_CS_PIN, 1);
}

static bool lcd_animation_callback(struct repeating_timer *t) {
    (void)t;

    draw_scrolling_totp();
    flush_buffer_to_lcd();
    return true;
}

void lcd_display_otp(const char *otp_code, int len) {
    (void)otp_code;
    (void)len;
}

void lcd_toggle_display(void) {
}

int lcd_display_red(void) {
    printf("[INFO] Starting LCD Display\n");

    if (DEV_Module_Init() != 0) {
        printf("[ERROR] DEV_Module_Init failed!\n");
        return -1;
    }

    DEV_SET_PWM(90);
    LCD_1IN47_Init(VERTICAL);

    LCD_1IN47_Clear(BLACK);

    text_buffer = (UBYTE *)malloc(LCD_1IN47.WIDTH * LINE_HEIGHT * NUM_LINES * 2);
    if (!text_buffer) {
        printf("[ERROR] Failed to allocate text buffer!\n");
        return -1;
    }

    add_repeating_timer_ms(20, lcd_animation_callback, NULL, &lcd_timer);
    printf("[INFO] LCD animation started!\n");

    return 0;
}

#else

int lcd_display_red(void) {
    return 0;
}

void lcd_display_otp(const char *otp_code, int len) {
    (void)otp_code;
    (void)len;
}

void lcd_toggle_display(void) {
}

#endif
