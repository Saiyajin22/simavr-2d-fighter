/*
	hd44780_glut.c

	Copyright Luki <humbell@ethz.ch>
	Copyright 2011 Michel Pollet <buserror@gmail.com>
	Copyright 2020 Akos Kiss <akiss@inf.u-szeged.hu>

 	This file is part of simavr.

	simavr is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	simavr is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with simavr.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hd44780_glut.h"

#if __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <pthread.h>
#include <stdio.h>

#include "hd44780_cgrom.h"

#define HD44780_GL_TEXTURE_WIDTH HD44780_CHAR_WIDTH
#define HD44780_GL_TEXTURE_HEIGHT (HD44780_CHAR_NUM * HD44780_CHAR_HEIGHT)
#define HD44780_GL_TEXTURE_BYTES_PER_PIXEL 4 // RGBA
#define HD44780_GL_TEXTURE_SIZE (HD44780_GL_TEXTURE_WIDTH * HD44780_GL_TEXTURE_HEIGHT * HD44780_GL_TEXTURE_BYTES_PER_PIXEL)
#define HD44780_GL_BORDER 3

static unsigned char cgrom_pixel_data[HD44780_GL_TEXTURE_SIZE];
static unsigned char cgram_pixel_data[HD44780_GL_TEXTURE_SIZE] = {0};  // NOTE: much more than actually necessary but ensures texture size identical to cgrom

static GLuint cgrom_texture;
static GLuint cgram_texture;

static pthread_mutex_t hd44780_state_mutex = PTHREAD_MUTEX_INITIALIZER;

void before_state_lock_cb(void *b)
{
	pthread_mutex_lock(&hd44780_state_mutex);
}

void after_state_lock_cb(void *b)
{
	pthread_mutex_unlock(&hd44780_state_mutex);
}

void hd44780_setup_mutex_for_gl(hd44780_t *b)
{
	b->on_state_lock = &before_state_lock_cb;
	b->on_state_lock_parameter = b;
	b->on_state_unlock = &after_state_lock_cb;
	b->on_state_unlock_parameter = b;
}

void
hd44780_gl_init()
{
	for (int c = 0; c < HD44780_CHAR_NUM; c++) {
		for (int y = 0; y < HD44780_CHAR_HEIGHT; y++) {
			uint8_t bits = hd44780_cgrom[c][y];
			uint8_t mask = 1 << (HD44780_CHAR_WIDTH - 1);
			for (int x = 0; x < HD44780_CHAR_WIDTH; x++, mask >>= 1) {
				int p = ((c * HD44780_CHAR_HEIGHT * HD44780_CHAR_WIDTH) + (y * HD44780_CHAR_WIDTH) + x) * HD44780_GL_TEXTURE_BYTES_PER_PIXEL;
				cgrom_pixel_data[p + 0] = 0;
				cgrom_pixel_data[p + 1] = 0;
				cgrom_pixel_data[p + 2] = 0;
				cgrom_pixel_data[p + 3] = bits & mask ? 0xff : 0;
			}
		}
	}

	glGenTextures(1, &cgrom_texture);
	glBindTexture(GL_TEXTURE_2D, cgrom_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			HD44780_GL_TEXTURE_WIDTH,
			HD44780_GL_TEXTURE_HEIGHT, 0, GL_RGBA,
	        GL_UNSIGNED_BYTE,
	        cgrom_pixel_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &cgram_texture);
	glBindTexture(GL_TEXTURE_2D, cgram_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, 4,
			HD44780_GL_TEXTURE_WIDTH,
			HD44780_GL_TEXTURE_HEIGHT, 0, GL_RGBA,
	        GL_UNSIGNED_BYTE,
	        cgram_pixel_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef(1.0f / (GLfloat) HD44780_GL_TEXTURE_WIDTH, 1.0f / (GLfloat) HD44780_GL_TEXTURE_HEIGHT, 1.0f);

	glMatrixMode(GL_MODELVIEW);
}

static inline void
glColor32U(uint32_t color)
{
	glColor4f(
			(float)((color >> 24) & 0xff) / 255.0f,
			(float)((color >> 16) & 0xff) / 255.0f,
			(float)((color >> 8) & 0xff) / 255.0f,
			(float)((color) & 0xff) / 255.0f );
}

static void
glputchar(uint8_t c,
		uint32_t character,
		uint32_t text,
		uint32_t shadow)
{
	int index = c;
	int left = 0;
	int right = HD44780_CHAR_WIDTH;
	int top = index * HD44780_CHAR_HEIGHT;
	int bottom = index * HD44780_CHAR_HEIGHT + HD44780_CHAR_HEIGHT;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDisable(GL_TEXTURE_2D);
	glColor32U(character);
	glBegin(GL_QUADS);
	glVertex3i(HD44780_CHAR_WIDTH, HD44780_CHAR_HEIGHT, 0);
	glVertex3i(0,                  HD44780_CHAR_HEIGHT, 0);
	glVertex3i(0,                  0,                   0);
	glVertex3i(HD44780_CHAR_WIDTH, 0,                   0);
	glEnd();

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, index < 16 ? cgram_texture : cgrom_texture);
	if (shadow) {
		glColor32U(shadow);
		glPushMatrix();
		glTranslatef(.2f, .2f, 0);
		glBegin(GL_QUADS);
		glTexCoord2i(right, top);     glVertex3i(HD44780_CHAR_WIDTH, 0,                   0);
		glTexCoord2i(left, top);      glVertex3i(0,                  0,                   0);
		glTexCoord2i(left, bottom);   glVertex3i(0,                  HD44780_CHAR_HEIGHT, 0);
		glTexCoord2i(right, bottom);  glVertex3i(HD44780_CHAR_WIDTH, HD44780_CHAR_HEIGHT, 0);
		glEnd();
		glPopMatrix();
	}
	glColor32U(text);
	glBegin(GL_QUADS);
	glTexCoord2i(right, top);     glVertex3i(HD44780_CHAR_WIDTH, 0,                   0);
	glTexCoord2i(left, top);      glVertex3i(0,                  0,                   0);
	glTexCoord2i(left, bottom);   glVertex3i(0,                  HD44780_CHAR_HEIGHT, 0);
	glTexCoord2i(right, bottom);  glVertex3i(HD44780_CHAR_WIDTH, HD44780_CHAR_HEIGHT, 0);
	glEnd();
}

void
hd44780_gl_draw(
		hd44780_t *b,
		uint32_t background,
		uint32_t character,
		uint32_t text,
		uint32_t shadow)
{
	if (b->on_state_lock != &before_state_lock_cb ||
		b->on_state_unlock != &after_state_lock_cb)
	{
		printf("Error: the hd44780 instance is not using the mutex of the OpenGL thread!\nCall hd44780_setup_mutex_for_gl() first!\n");
		exit(EXIT_FAILURE);
	}

	int rows = b->w;
	int lines = b->h;

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	glColor32U(background);
	glTranslatef(HD44780_GL_BORDER, HD44780_GL_BORDER, 0);
	glBegin(GL_QUADS);
	glVertex3f(rows * HD44780_CHAR_WIDTH + (rows - 1) + HD44780_GL_BORDER, -HD44780_GL_BORDER, 0);
	glVertex3f(-HD44780_GL_BORDER, -HD44780_GL_BORDER, 0);
	glVertex3f(-HD44780_GL_BORDER, lines * HD44780_CHAR_HEIGHT + (lines - 1) + HD44780_GL_BORDER, 0);
	glVertex3f(rows * HD44780_CHAR_WIDTH + (rows - 1) + HD44780_GL_BORDER, lines * HD44780_CHAR_HEIGHT
	        + (lines - 1) + HD44780_GL_BORDER, 0);
	glEnd();

	// create a local copy (so we can release the mutex as fast as possible)
	uint8_t vram[192];
	pthread_mutex_lock(&hd44780_state_mutex);
	int cgram_dirty = hd44780_get_flag(b, HD44780_FLAG_CRAM_DIRTY);
	for (uint8_t i = 0; i < 192; i++)
		vram[i] = b->vram[i];
	// the values have been seen, they are not dirty anymore
	hd44780_set_flag(b, HD44780_FLAG_CRAM_DIRTY, 0);
	hd44780_set_flag(b, HD44780_FLAG_DIRTY, 0);
	pthread_mutex_unlock(&hd44780_state_mutex);

	// Re-generate texture for cgram
	if (cgram_dirty) {
		for (int c = 0; c < 8; c++) {
			for (int y = 0; y < HD44780_CHAR_HEIGHT; y++) {
				uint8_t bits = vram[0x80 + c * 8 + y];
				uint8_t mask = 1 << (HD44780_CHAR_WIDTH - 1);
				for (int x = 0; x < HD44780_CHAR_WIDTH; x++, mask >>= 1) {
					int p1 = ((c * HD44780_CHAR_HEIGHT * HD44780_CHAR_WIDTH) + (y * HD44780_CHAR_WIDTH) + x) * HD44780_GL_TEXTURE_BYTES_PER_PIXEL;
					cgram_pixel_data[p1 + 0] = 0;
					cgram_pixel_data[p1 + 1] = 0;
					cgram_pixel_data[p1 + 2] = 0;
					cgram_pixel_data[p1 + 3] = bits & mask ? 0xff : 0;

					int p2 = (((c + 8) * HD44780_CHAR_HEIGHT * HD44780_CHAR_WIDTH) + (y * HD44780_CHAR_WIDTH) + x) * HD44780_GL_TEXTURE_BYTES_PER_PIXEL;
					cgram_pixel_data[p2 + 0] = 0;
					cgram_pixel_data[p2 + 1] = 0;
					cgram_pixel_data[p2 + 2] = 0;
					cgram_pixel_data[p2 + 3] = bits & mask ? 0xff : 0;
				}
			}
		}

		// FIXME: unsure how much of this is actually necessary
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, cgram_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, 4,
				HD44780_GL_TEXTURE_WIDTH,
				HD44780_GL_TEXTURE_HEIGHT, 0, GL_RGBA,
		        GL_UNSIGNED_BYTE,
		        cgram_pixel_data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glDisable(GL_TEXTURE_2D);
	}

	glColor3f(1.0f, 1.0f, 1.0f);
	const uint8_t offset[] = { 0x00, 0x40, 0x00 + 20, 0x40 + 20 };
	for (int v = 0 ; v < lines; v++) {
		glPushMatrix();
		for (int i = 0; i < rows; i++) {
			glputchar(vram[offset[v] + i], character, text, shadow);
			glTranslatef(HD44780_CHAR_WIDTH + 1, 0, 0);
		}
		glPopMatrix();
		glTranslatef(0, HD44780_CHAR_HEIGHT + 1, 0);
	}
}
