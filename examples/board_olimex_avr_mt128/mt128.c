/*
	m128.c

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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "sim_elf.h"
#include "sim_gdb.h"

#if __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <pthread.h>

#include "button.h"
#include "hd44780.h"
#include "hd44780_glut.h"


/* Command line argument globals */

static const char * arg0;
static struct {
	int verbose;
	int debug;
	char *firmware;
} args = {
	.verbose = 1,
	.debug = 0,
	.firmware = NULL,
};

/* Simulated component globals */

typedef struct { // UI key state
	int count; // state change counter (~timestamp of last change)
	uint8_t state; // key state (0: pressed, 1: released)
} key_state_t;

static struct {
	button_t button; // simulated button
	char key; // ASCII code of associated UI key
	volatile key_state_t ui_key_state; // current UI key state
	key_state_t avr_key_state; // last UI key state change translated to AVR button press/release
	const char * name; // symbolic name
} button[5] = {
	{ .key='8', .ui_key_state = { .count = 0, .state = 1 }, .avr_key_state = { .count = 0, .state = 1 }, .name = "B1" },
	{ .key='4', .ui_key_state = { .count = 0, .state = 1 }, .avr_key_state = { .count = 0, .state = 1 }, .name = "B2" },
	{ .key='5', .ui_key_state = { .count = 0, .state = 1 }, .avr_key_state = { .count = 0, .state = 1 }, .name = "B3" },
	{ .key='6', .ui_key_state = { .count = 0, .state = 1 }, .avr_key_state = { .count = 0, .state = 1 }, .name = "B4" },
	{ .key='2', .ui_key_state = { .count = 0, .state = 1 }, .avr_key_state = { .count = 0, .state = 1 }, .name = "B5" },
};
static uint8_t rotation = 0; // Rotation of LCD screen, between 0 and 3
static hd44780_t hd44780; // simulated LCD controller
static avr_t * avr = NULL; // simulated AVR MCU


/* Command line processing */

/**
 * Print single line of usage information.
 * Expects arg0 to be already initialized.
 */
static void print_usage()
{
	printf("usage: %s [-h] [-v] [-g] [GLUT options...] FILE\n", arg0); // NOTE: keep in sync with getopt's optstring
}

/**
 * Print detailed help on usage and command line arguments.
 * Expects arg0 to be already initialized.
 */
static void print_help()
{
	print_usage();
	printf(  // NOTE: keep in sync with getopt's optstring
		"\n"
		"positional arguments:\n"
		" FILE     firmware to load\n"
		"\n"
		"optional arguments:\n"
		" -h       show this help message and exit\n"
		" -v       increase verbosity level (can be passed more than once)\n"
		" -g       listen for gdb connection on port 1234\n"
		"\n"
		"GLUT options:\n"
		" windows system dependent\n"
	);
}

/**
 * Extract and process GLUT-specific command line arguments.
 * It also initializes the GLUT library.
 * Must be called before any other command line processing as it changes
 * argc/argv.
 */
static void process_ui_args(int * pargc, char *argv[])
{
	glutInit(pargc, argv); /* initialize GLUT system */
}

/**
 * Process command line arguments.
 * Initializes arg0 and args.
 */
static void process_args(int argc, char *argv[])
{
	arg0 = argv[0];

	int opt;
	while ((opt = getopt(argc, argv, "hvg")) != -1) {
		switch (opt) {
			case 'h':
				print_help();
				exit(EXIT_SUCCESS);
			case 'v':
				args.verbose++;
				break;
			case 'g':
				args.debug++;
				break;
			default:
				print_usage();
				exit(EXIT_FAILURE);
		}
	}

	if (optind != argc - 1) {
		print_usage();
		exit(EXIT_FAILURE);
	}
	args.firmware = argv[optind];
}


/* Setup of AVR (firmware, MCU, peripherals, connections) */

/**
 * Initialize the MCU and load the firmware.
 * Configure debug functionality (log level, debugger connection).
 */
static void setup_avr()
{
	elf_firmware_t f = {{0}};
	if (elf_read_firmware(args.firmware, &f) == -1) {
		fprintf(stderr, "%s: Unable to load firmware from file %s\n", arg0, args.firmware);
		exit(EXIT_FAILURE);
	};
	printf("firmware %s f=%d mmcu=%s\n", args.firmware, (int) f.frequency, f.mmcu);

	avr = avr_make_mcu_by_name(f.mmcu);
	if (!avr) {
		fprintf(stderr, "%s: AVR '%s' not known\n", arg0, f.mmcu);
		exit(EXIT_FAILURE);
	}

	avr_init(avr);
	avr->log = args.verbose;

	avr_load_firmware(avr, &f);

	avr->gdb_port = 1234; // even if not setup at startup, activate gdb if crashing
	if (args.debug) {
		avr->state = cpu_Stopped;
		avr_gdb_init(avr);
	}
}

/**
 * Initialize peripherals and connect them to the MCU.
 */
static void setup_board()
{
	for (int i = 0; i < 5; i++) {
		// initialize our peripheral
		button_init(avr, &button[i].button, button[i].name);
		// "connect" the output irq of the button to the port pin of the AVR
		avr_connect_irq(button[i].button.irq + IRQ_BUTTON_OUT, avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('A'), i));
	}

	hd44780_init(avr, &hd44780, 16, 2);
	/* Connect Data Lines to Port C, 4-7 */
	/* These are bidirectional too */
	for (int i = 0; i < 4; i++) {
		avr_irq_t * iavr = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 4 + i);
		avr_irq_t * ilcd = hd44780.irq + IRQ_HD44780_D4 + i;
		// AVR -> LCD
		avr_connect_irq(iavr, ilcd);
		// LCD -> AVR
		avr_connect_irq(ilcd, iavr);
	}
	avr_connect_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0), hd44780.irq + IRQ_HD44780_RS);
	avr_connect_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 1), hd44780.irq + IRQ_HD44780_RW);
	avr_connect_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 2), hd44780.irq + IRQ_HD44780_E);
}


/* Setup of UI */

typedef struct {
	int w;
	int h;
} window_size_t;

static window_size_t ui_get_window_size()
{
	// NOTE: keep constants in sync with hd44780_glut.c and hd44780_cgrom.h
	const int border = 3;	  // HD44780_GL_BORDER
	const int charwidth = 5;  // HD44780_CHAR_WIDTH
	const int charheight = 8; // HD44780_CHAR_HEIGHT

	const int pixsize = 3;
	window_size_t result={};
	result.w = ((2 * border - 1) + hd44780.w * (charwidth + 1)) * pixsize;
	result.h = ((2 * border - 1) + hd44780.h * (charheight + 1)) * pixsize;
	return result;
}

static void ui_set_window_rotation()
{
	window_size_t size = ui_get_window_size();

	glMatrixMode(GL_PROJECTION); // Select projection matrix
	glLoadIdentity();			 // Start with an identity matrix
	switch (rotation) {
	case 0:
		glOrtho(0, size.w, 0, size.h, 0, 10);
		glScalef(1, -1, 1);
		glTranslatef(0, -size.h, 0);
		glutReshapeWindow(size.w, size.h);
		break;
	case 1:
		glOrtho(0, size.h, 0, size.w, 0, 10);
		glScalef(1, -1, 1);
		glTranslatef(size.h, -size.w, 0);
		glRotatef(90, 0, 0, 1);
		glutReshapeWindow(size.h, size.w);
		break;
	case 2:
		glOrtho(0, size.w, 0, size.h, 0, 10);
		glScalef(1, -1, 1);
		glTranslatef(size.w, 0, 0);
		glRotatef(180, 0, 0, 1);
		glutReshapeWindow(size.w, size.h);
		break;
	default:
		glOrtho(0, size.h, 0, size.w, 0, 10);
		glScalef(1, -1, 1);
		glRotatef(270, 0, 0, 1);
		glutReshapeWindow(size.h, size.w);
		break;
	}
}

static void rotate_ui(){
	rotation++;
	if (rotation >= 4) {
		rotation = 0;
	}
	ui_set_window_rotation();
}

/**
 * Draw window contents.
 */
static void ui_display_cb(void)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_MODELVIEW); // Select modelview matrix
	glPushMatrix();
	glLoadIdentity(); // Start with an identity matrix
	glScalef(3, 3, 1);

	hd44780_gl_draw(&hd44780, 0x00aa00ff, 0x00cc00ff, 0x000000ff, 0x00000055); // fluo green

	glPopMatrix();
	glutSwapBuffers();
}

/**
 * Trigger window redraw periodically.
 */
static void ui_display_timer_cb(int i)
{
	glutTimerFunc(1000 / 64, ui_display_timer_cb, 0); // restart timer
	glutPostRedisplay();
}

/**
 * Handle key presses in window.
 */
static void ui_key_press_cb(unsigned char key, int x, int y)
{
	if (key == 'q') {
		exit(EXIT_SUCCESS);
	}
	if (key == 'r') {
		rotate_ui();
		// release buttons
		for (int i = 0; i < 5; i++) {
			if (!button[i].ui_key_state.state) {
				button[i].ui_key_state.count++;
				button[i].ui_key_state.state = 1;
			}
		}
		// permute corresponding keys
		char tmp = button[0].key;
		button[0].key = button[3].key;
		button[3].key = button[4].key;
		button[4].key = button[1].key;
		button[1].key = tmp;
		return;
	}

	for (int i = 0; i < 5; i++) {
		if (key == button[i].key && button[i].ui_key_state.state) {
			button[i].ui_key_state.count++;
			button[i].ui_key_state.state = 0;
			break;
		}
	}
}

/**
 * Handle key releases in window.
 */
static void ui_key_release_cb(unsigned char key, int x, int y)
{
	for (int i = 0; i < 5; i++) {
		if (key == button[i].key && !button[i].ui_key_state.state) {
			button[i].ui_key_state.count++;
			button[i].ui_key_state.state = 1;
			break;
		}
	}
}

static void setup_ui()
{
	window_size_t size = ui_get_window_size();

	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	glutInitWindowSize(size.w, size.h);
	glutCreateWindow("Press 'q' to quit");	/* create window */

	ui_set_window_rotation();

	glutDisplayFunc(ui_display_cb);		/* set window's display callback */
	glutTimerFunc(1000 / 24, ui_display_timer_cb, 0);

	glutKeyboardFunc(ui_key_press_cb);		/* set window's key press callback */
	glutKeyboardUpFunc(ui_key_release_cb);	/* set window's key release callback */
	glutIgnoreKeyRepeat(1);

	glEnable(GL_TEXTURE_2D);
	glShadeModel(GL_SMOOTH);

	glClearColor(0.8f, 0.8f, 0.8f, 1.0f);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	hd44780_gl_init();
	hd44780_setup_mutex_for_gl(&hd44780);
}


/* Threads (AVR & UI) */

/**
 * Run AVR simulation: step MCU infinitely and translate UI key events to
 * simulated button presses/releases.
 */
static void * run_avr_thread(void * ignore)
{
	while (1) {
		avr_run(avr);

		for (int i = 0; i < 5; i++) {
			if (button[i].avr_key_state.count != button[i].ui_key_state.count && button[i].avr_key_state.state != button[i].ui_key_state.state) {
				button[i].avr_key_state = button[i].ui_key_state;
				if (button[i].avr_key_state.state == 0) {
					button_press(&button[i].button, 0);
					// printf("Button %s pressed\n", button[i].name);
				} else {
					button_release(&button[i].button);
					// printf("Button %s released\n", button[i].name);
				}
			}
		}
	}

	return NULL;
}

/**
 * Run AVR simulation in a thread.
 */
static void run_avr()
{
	pthread_t run;
	pthread_create(&run, NULL, run_avr_thread, NULL);
}

/**
 * Run UI event loop.
 * Does not return.
 */
static void run_ui()
{
	glutMainLoop();
}


/* Entry point */

int main(int argc, char *argv[])
{
	process_ui_args(&argc, argv);
	process_args(argc, argv);

	setup_avr();
	setup_board();
	setup_ui();

	printf("This is an Olimex AVR-MT128 development board simulation\n"
		"Press 'q' to exit\n"
		"Press 'r' to rotate the display\n"
		"Press '8', '4', '5', '6', or '2' for buttons B1..B5\n");

	run_avr();
	run_ui();
}
