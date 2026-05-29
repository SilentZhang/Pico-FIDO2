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

int lcd_display_red(void) {
    printf("Initializing LCD display...\n");
    
    if (DEV_Module_Init() != 0) {
        printf("LCD module init failed!\n");
        return -1;
    }
    
    DEV_SET_PWM(100);
    LCD_1IN47_Init(VERTICAL);
    
    printf("Clearing LCD with red color...\n");
    LCD_1IN47_Clear(RED);
    
    printf("LCD display red complete!\n");
    
    return 0;
}

