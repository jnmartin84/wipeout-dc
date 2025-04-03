#include "../system.h"
#include "../input.h"
#include "../utils.h"
#include "../types.h"
#include "../mem.h"
#include "../platform.h"

#include "intro.h"
#include "ui.h"
#include "image.h"
#include "game.h"

static void intro_end(void);

void intro_init(void) {
	intro_end();
	return;
}

static void intro_end(void) {
	game_set_scene(GAME_SCENE_TITLE);
}

void intro_update(void) {
}