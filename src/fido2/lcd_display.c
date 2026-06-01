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

#define LINE_HEIGHT 19
#define NUM_LINES 5
#define TOTP_PERIOD 30
#define MAX_TOTP_CRED 64

#define TAG_NAME            0x71
#define TAG_KEY             0x73
#define OATH_TYPE_TOTP      0x20
#define OATH_TYPE_MASK      0xf0
#define EF_RTC_OFFSET       0xBB04

typedef struct {
    uint8_t key[64];
    int key_len;
    char name[32];
} totp_cred_t;

static UBYTE *text_buffer = NULL;
static struct repeating_timer lcd_timer;
static totp_cred_t *totp_creds = NULL;
static int num_totp_creds = 0;
static float rainbow_offset = 0.0f;
static float text_offset = 0.0f;
static char line_texts[NUM_LINES][128];
static int line_text_lens[NUM_LINES];
static uint32_t last_totp_time = 0;

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

static void extract_org_name(char *name) {
    char *colon_pos = strchr(name, ':');
    if (colon_pos) {
        *colon_pos = '\0';
    } else {
        char *at_pos = strchr(name, '@');
        if (at_pos) {
            *at_pos = '\0';
        }
    }
    
    size_t len = strlen(name);
    if (len > 20) {
        name[20] = '\0';
    }
}

static void find_totp_credentials(void) {
    num_totp_creds = 0;

    if (totp_creds != NULL) {
        free(totp_creds);
        totp_creds = NULL;
    }

    int temp_count = 0;
    for (int i = 0; i < 255 && temp_count < MAX_TOTP_CRED; i++) {
        file_t *ef = search_dynamic_file((uint16_t)(0xBA00 + i));
        if (file_has_data(ef)) {
            const uint8_t *ef_data = file_get_data(ef);
            size_t ef_len = file_get_size(ef);

            asn1_ctx_t ctx;
            asn1_ctx_init((uint8_t *)ef_data, (uint16_t)ef_len, &ctx);

            asn1_ctx_t key_ctx;
            if (asn1_find_tag(&ctx, TAG_KEY, &key_ctx) == true) {
                if ((key_ctx.data[0] & OATH_TYPE_MASK) == OATH_TYPE_TOTP) {
                    temp_count++;
                }
            }
        }
    }

    if (temp_count == 0) {
        return;
    }

    totp_creds = (totp_cred_t *)malloc(temp_count * sizeof(totp_cred_t));
    if (totp_creds == NULL) {
        return;
    }

    memset(totp_creds, 0, temp_count * sizeof(totp_cred_t));

    for (int i = 0; i < 255 && num_totp_creds < temp_count; i++) {
        file_t *ef = search_dynamic_file((uint16_t)(0xBA00 + i));
        if (file_has_data(ef)) {
            const uint8_t *ef_data = file_get_data(ef);
            size_t ef_len = file_get_size(ef);

            asn1_ctx_t ctx;
            asn1_ctx_init((uint8_t *)ef_data, (uint16_t)ef_len, &ctx);

            asn1_ctx_t key_ctx, name_ctx;
            if (asn1_find_tag(&ctx, TAG_KEY, &key_ctx) == true) {
                if ((key_ctx.data[0] & OATH_TYPE_MASK) == OATH_TYPE_TOTP) {
                    totp_cred_t *cred = &totp_creds[num_totp_creds];
                    cred->key_len = key_ctx.len;
                    memcpy(cred->key, key_ctx.data, key_ctx.len);

                    if (asn1_find_tag(&ctx, TAG_NAME, &name_ctx) == true) {
                        int name_len = name_ctx.len;
                        if (name_len > sizeof(cred->name) - 1) name_len = sizeof(cred->name) - 1;
                        memcpy(cred->name, name_ctx.data, name_len);
                        cred->name[name_len] = '\0';
                        extract_org_name(cred->name);
                    }
                    num_totp_creds++;
                }
            }
        }
    }
}

static bool is_time_synced(void) {
    file_t *ef = search_dynamic_file(EF_RTC_OFFSET);
    if (ef && file_has_data(ef) && file_get_size(ef) >= 1) {
        const uint8_t *data = file_get_data(ef);
        if (data[0] == 0xAA) {
            return true;
        }
    }
    return false;
}

static void draw_three_totp_lines(void) {
    time_t now_sec = get_rtc_time();
    uint64_t time_val = (uint64_t) now_sec / TOTP_PERIOD;
    uint32_t remaining = TOTP_PERIOD - ((uint32_t) now_sec % TOTP_PERIOD);

    if (time_val != last_totp_time || num_totp_creds == 0) {
        last_totp_time = (uint32_t) time_val;

        if (num_totp_creds == 0) {
            find_totp_credentials();
        }
    }

    int max_positions = 0;
    for (int i = 0; i < NUM_LINES - 1; i++) {
        int cred_idx = i;
        int positions = 0;
        while (cred_idx < num_totp_creds) {
            positions++;
            cred_idx += (NUM_LINES - 1);
        }
        if (positions > max_positions) {
            max_positions = positions;
        }
    }

    for (int i = 0; i < NUM_LINES - 1; i++) {
        line_texts[i + 1][0] = '\0';
        int current_len = 0;
        int cred_idx = i;
        for (int pos = 0; pos < max_positions; pos++) {
            char single_line[64];
            if (cred_idx < num_totp_creds && totp_creds != NULL) {
                char otp[12];
                int otp_len = 0;
                calculate_totp(totp_creds[cred_idx].key, totp_creds[cred_idx].key_len, time_val, otp, &otp_len);
                snprintf(single_line, sizeof(single_line), "%-12s: %-8s | ", totp_creds[cred_idx].name, otp);
            } else {
                snprintf(single_line, sizeof(single_line), "%-12s: %-8s | ", "--", "--");
            }
            if (current_len + strlen(single_line) < sizeof(line_texts[i + 1])) {
                strcat(line_texts[i + 1], single_line);
                current_len += strlen(single_line);
            }
            cred_idx += (NUM_LINES - 1);
        }
        line_text_lens[i + 1] = strlen(line_texts[i + 1]);
    }

    sFONT *font = &Font16;
    int char_width = font->Width;

    int max_len = 0;
    for (int i = 1; i < NUM_LINES; i++) {
        if (line_text_lens[i] > max_len) max_len = line_text_lens[i];
    }

    bool synced = is_time_synced();
    if (synced) {
        snprintf(line_texts[0], sizeof(line_texts[0]), "[*] %lus", (unsigned long)remaining);
    } else {
        snprintf(line_texts[0], sizeof(line_texts[0]), "[ ] %lus", (unsigned long)remaining);
    }
    line_text_lens[0] = strlen(line_texts[0]);

    fill_text_buffer_black();

    int scroll_chars = (int)(text_offset / char_width);
    float char_fraction = (text_offset - scroll_chars * char_width) / (float)char_width;
    int start_x = -(int)(char_fraction * char_width);

    for (int line_idx = 0; line_idx < NUM_LINES; line_idx++) {
        char *scroll_text = line_texts[line_idx];
        int text_len = line_text_lens[line_idx];
        int y_pos = line_idx * LINE_HEIGHT;

        if (line_idx == 0) {
            for (int i = 0; i < text_len; i++) {
                char ch = scroll_text[i];
                int x_pos = i * char_width;
                if (x_pos > (int)LCD_1IN47.WIDTH) break;
                if (x_pos + char_width < 0) continue;

                UWORD color = rgb_to_565(255, 255, 255);
                draw_char_to_buffer((UWORD)x_pos, (UWORD)y_pos, ch, font, color);
            }
        } else {
            float line_rainbow_offset = rainbow_offset + (float)line_idx / NUM_LINES;

            for (int i = 0; i < text_len + 2; i++) {
                int idx = (scroll_chars + i) % text_len;
                char ch = scroll_text[idx];

                int x_pos = start_x + i * char_width;
                if (x_pos > (int)LCD_1IN47.WIDTH) continue;
                if (x_pos + char_width < 0) continue;

                float color_ratio = (float)(scroll_chars + i) / (float)text_len;
                color_ratio += line_rainbow_offset;
                while (color_ratio > 1.0f) color_ratio -= 1.0f;

                UWORD color;
                get_rainbow_color(color_ratio, &color);

                draw_char_to_buffer((UWORD)x_pos, (UWORD)y_pos, ch, font, color);
            }
        }
    }

    rainbow_offset += 0.005f;
    if (rainbow_offset >= 1.0f) rainbow_offset = 0.0f;

    text_offset += 2.0f;

    int max_scroll_width = max_len * char_width;
    if (max_scroll_width > 0 && text_offset >= max_scroll_width) {
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

    draw_three_totp_lines();
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

    find_totp_credentials();

    bool synced = is_time_synced();
    if (synced) {
        snprintf(line_texts[0], sizeof(line_texts[0]), "[*] --");
    } else {
        snprintf(line_texts[0], sizeof(line_texts[0]), "[ ] --");
    }
    line_text_lens[0] = strlen(line_texts[0]);

    for (int i = 0; i < NUM_LINES - 1; i++) {
        if (i < num_totp_creds) {
            strcpy(line_texts[i + 1], "Loading...");
        } else {
            strcpy(line_texts[i + 1], "-");
        }
        line_text_lens[i + 1] = strlen(line_texts[i + 1]);
    }

    draw_three_totp_lines();
    flush_buffer_to_lcd();
    
#ifdef PICO_PLATFORM
    add_repeating_timer_ms(20, lcd_animation_callback, NULL, &lcd_timer);
#endif

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
