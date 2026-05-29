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

#if ENABLE_LCD
#include "lcd_display.h"
#include <stdio.h>

int picokey_init() {
    printf("Pico FIDO2 - LCD red display test\n");
    lcd_display_red();
    return 0;
}
#endif

