/*
 * This file is part of the TREZOR project.
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "trezor.h"
#include "oled.h"
#include "bitmaps.h"
#include "util.h"
#include "usb.h"
#include "setup.h"
#include "storage.h"
#include "layout.h"
#include "layout2.h"
#include "rng.h"

#include "../buttons.h"
#include "../rng.h"

uint32_t __stack_chk_guard;

void __attribute__((noreturn)) __stack_chk_fail(void)
{
	layoutDialog(DIALOG_ICON_ERROR, NULL, NULL, NULL, "Stack smashing", "detected.", NULL, "Please unplug", "the device.", NULL);
	for (;;) {} // loop forever
}

enum GameDir {
    DIR_UP,
    DIR_RIGHT,
    DIR_DOWN,
    DIR_LEFT
};

enum GameState {
    STATE_PLAYING,
    STATE_GAMEOVER
};

struct Game {
    int16_t field[OLED_WIDTH * OLED_HEIGHT];
    int16_t len;
    int16_t bug_lifetime; // negative number, grows to zero
    uint32_t delay; // how much to sleep after each game loop iter
    uint32_t spawn_rate; // spawn_rate < random32() => a bug spawns
    int growth_rate; // how much to grow after eating
    int x;
    int y;
    enum GameDir dir;
    enum GameState state;
};

static struct Game game;

#define GAME_CELL(x, y) game.field[y * OLED_WIDTH + x]

void gameInit(void)
{
    memset(game.field, 0, sizeof(game.field));
    game.state = STATE_PLAYING;
    game.dir = DIR_DOWN;
    game.x = 25;
    game.y = 0;
    game.len = 16;
    game.delay = 1e6;
    game.growth_rate = 3;
    game.bug_lifetime = INT16_MIN / 128;
    game.spawn_rate = UINT32_MAX - (UINT32_MAX / 32);
}

void gamePlayingUpdate(void)
{
    // input

    buttonUpdate();
    if (button.YesUp) { // right
        switch (game.dir) {
        case DIR_UP: game.dir = DIR_RIGHT; break;
        case DIR_LEFT: game.dir = DIR_UP; break;
        case DIR_DOWN: game.dir = DIR_LEFT; break;
        case DIR_RIGHT: game.dir = DIR_DOWN; break;
        }
    }
    if (button.NoUp) { // left
        switch (game.dir) {
        case DIR_UP: game.dir = DIR_LEFT; break;
        case DIR_LEFT: game.dir = DIR_DOWN; break;
        case DIR_DOWN: game.dir = DIR_RIGHT; break;
        case DIR_RIGHT: game.dir = DIR_UP; break;
        }
    }

    // update head

    switch (game.dir) {
    case DIR_UP: game.y--; break;
    case DIR_LEFT: game.x--; break;
    case DIR_DOWN: game.y++; break;
    case DIR_RIGHT: game.x++; break;
    }

    // collisions

    if ((game.x < 0 || game.x >= OLED_WIDTH) || // hit left or right
        (game.y < 0 || game.y >= OLED_HEIGHT)) // hit top or bottom
    {
        game.state = STATE_GAMEOVER;
        return;
    }

    if (GAME_CELL(game.x, game.y) > 0) { // hit the body
        game.state = STATE_GAMEOVER;
        return;
    }

    if (GAME_CELL(game.x, game.y) < 0) { // ate a bug
        game.len++; // cell gets replaced by head later
    }

    // move

    int y, x;

    for (y = 0; y < OLED_HEIGHT; y++) {
        for (x = 0; x < OLED_WIDTH; x++) {
            if (GAME_CELL(x, y) > 0) {
                GAME_CELL(x, y)--;
            } else if (GAME_CELL(x, y) < 0) {
                GAME_CELL(x, y)++;
            }
        }
    }

    GAME_CELL(game.x, game.y) = game.len;

    // spawn a bug maybe?

    if (game.spawn_rate < random32()) {
        int16_t bug_x = random32() % OLED_WIDTH;
        int16_t bug_y = random32() % OLED_HEIGHT;
        GAME_CELL(bug_x, bug_y) = INT16_MIN / 128;
    }
}

void gamePlayingDraw(void)
{
    int y, x;

    for (y = 0; y < OLED_HEIGHT; y++) {
        for (x = 0; x < OLED_WIDTH; x++) {
            if (GAME_CELL(x, y) != 0) {
                oledDrawPixel(x, y);
            }
        }
    }
}

void gameOverUpdate(void) {
    buttonUpdate();
    if (button.YesUp) {
        gameInit();
    }
}

void gameOverDraw(void)
{
    oledDrawStringCenter(OLED_HEIGHT / 2, "GAME OVER");
}

void gameUpdate(void)
{
    switch (game.state) {
    case STATE_PLAYING: gamePlayingUpdate(); break;
    case STATE_GAMEOVER: gameOverUpdate(); break;
    }
}

void gameDraw(void)
{
    oledClear();
    switch (game.state) {
    case STATE_PLAYING: gamePlayingDraw(); break;
    case STATE_GAMEOVER: gameOverDraw(); break;
    }
    oledRefresh();
}

int main(void)
{
	__stack_chk_guard = random32();
#ifndef APPVER
	setup();
	oledInit();
#else
	setupApp();
#endif
#if DEBUG_LINK
	oledSetDebug(1);
	storage_reset(); // wipe storage if debug link
	storage_reset_uuid();
	storage_commit();
#endif

	oledDrawBitmap(40, 0, &bmp_logo64);
	oledRefresh();

    gameInit();
    for (;;) {
        gameUpdate();
        gameDraw();
        delay(game.delay);
    }

	/* storage_init(); */
	/* layoutHome(); */
	/* usbInit(); */
	/* for (;;) { */
	/* 	usbPoll(); */
	/* } */

	return 0;
}
