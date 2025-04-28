#include "../system.h"
#include "../input.h"
#include "../utils.h"

#include "title.h"
#include "ui.h"
#include "image.h"
#include "game.h"

static uint16_t title_image;
static float start_time;
static bool has_shown_attract = false;

extern global_render_state_t render_state;

void title_init(void) {
	render_state.LOAD_UNFILTERED = 1;
	title_image = image_get_texture("wipeout/textures/wiptitle.tim");
	render_state.LOAD_UNFILTERED = 0;
	start_time = system_time();
	sfx_music_mode(SFX_MUSIC_RANDOM);
	sfx_music_play(rand_int(0, len(def.music)));
}

void title_update(void) {
	render_set_view_2d();
	render_push_2d(vec2i(0, 0), render_size(), rgba(255,255,255,255), title_image);
	ui_draw_text_centered("PRESS START", ui_scaled_pos(UI_POS_BOTTOM | UI_POS_CENTER, vec2i(0, -40)), UI_SIZE_8, UI_COLOR_DEFAULT);

	if (input_pressed(A_MENU_SELECT) || input_pressed(A_MENU_START)) {
		sfx_play(SFX_MENU_SELECT);
		game_set_scene(GAME_SCENE_MAIN_MENU);
	} else {
	}

	float duration = system_time() - start_time;
	if (
		(has_shown_attract && duration > 7) ||
		(duration > 30)
	) {
		sfx_music_mode(SFX_MUSIC_RANDOM);
		has_shown_attract = true;
		g.is_attract_mode = true;
		g.pilot = rand_int(0, len(def.pilots));
		g.circut = rand_int(0, NUM_NON_BONUS_CIRCUTS);
		g.race_class = rand_int(0, NUM_RACE_CLASSES);
		g.race_type = RACE_TYPE_SINGLE;
		sfx_music_pause();
		game_set_scene(GAME_SCENE_RACE);
	}
}
