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
extern volatile uint32_t last_five_tracks[5];
void title_init(void) {
	render_state.LOAD_UNFILTERED = 1;
	title_image = image_get_texture("wipeout/textures/wiptitle.tim");
	render_state.LOAD_UNFILTERED = 0;
	start_time = system_time();
	sfx_music_mode(SFX_MUSIC_RANDOM);
	uint32_t new_index = rand_int(0, len(def.music));

	int try_again = 0;
	for (int li_idx = 0; li_idx < 5; li_idx++) {
		if (last_five_tracks[li_idx] == new_index) {
			try_again = 1;
			break;
		}
	}
	// never repeat a song in random, and try not to repeat last 5 unique music indices
	while (try_again) {
		try_again = 0;
		new_index = rand_int(0, len(def.music));
		for (int li_idx = 0; li_idx < 5; li_idx++) {
			if (last_five_tracks[li_idx] == new_index) {
				try_again = 1;
				break;
			}
		}
	}

	for (int i = 1; i < 5; i++) {
		last_five_tracks[i - 1] = last_five_tracks[i];
	}

	last_five_tracks[4] = new_index;

	sfx_music_play(new_index);
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
