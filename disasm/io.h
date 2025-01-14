/*
 * MyJIT Disassembler
 *
 * Copyright (C) 2015 Petr Krajca, <petr.krajca@upol.cz>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define OUTPUT_BYTES_PER_LINE   (8)

void output_color_normal();
void output_color_white();
void output_color_yellow();
void output_color_cyan();
void output_code(uintptr_t  addr, unsigned char *data, int size, char *text);

void input_init();
void input_free();
void input_clear();
unsigned char *input_buffer();
int input_size();
int input_read();
void input_convert();
