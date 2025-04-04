#include "../utils.h"
#include "../system.h"
#include "../mem.h"
#include "../platform.h"
#include "../input.h"

#include "menu.h"
#include "main_menu.h"
#include "game.h"
#include "image.h"
#include "ui.h"

static void page_main_init(menu_t *menu);
static void page_options_init(menu_t *menu);
static void page_race_class_init(menu_t *menu);
static void page_race_type_init(menu_t *menu);
static void page_team_init(menu_t *menu);
static void page_pilot_init(menu_t *menu);
static void page_circut_init(menu_t *menu);
static void page_options_controls_init(menu_t *menu);
static void page_options_video_init(menu_t *menu);
static void page_options_audio_init(menu_t *menu);
static void page_options_highscores_init(menu_t *menu);
static void page_options_bonus_init(menu_t *menu);

int video_probably_slow = 0;
int in_menu = 0;

static uint16_t background;
static texture_list_t track_images;
static menu_t *main_menu;

static struct {
	Object *race_classes[2];
	Object *teams[4];
	Object *pilots[8];
	struct { Object *stopwatch, *save, *load, *headphones, *cd; } options;
	struct { Object *championship, *msdos, *single_race, *options; } misc;
	Object *rescue;
	Object *controller;
} models;

extern int test_en;
extern int dep_en;
static void draw_model(Object *model, vec2_t offset, vec3_t pos, float rotation) {
	render_set_view(vec3(0,0,0), vec3(0, -F_PI, -F_PI));
	render_set_screen_position(offset);
	mat4_t mat = mat4_identity();
	mat4_set_translation(&mat, pos);
	mat4_set_yaw_pitch_roll(&mat, vec3(0, rotation, F_PI));
	object_draw(model, &mat);
	render_set_screen_position(vec2(0, 0));
}

// -----------------------------------------------------------------------------
// Main Menu

static void button_start_game(menu_t *menu, int data) {
	page_race_class_init(menu);
}

static void button_options(menu_t *menu, int data) {
	page_options_init(menu);
}

static void page_main_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(g.ships[0].model, vec2(0, -0.1), vec3(0, 0, -700), system_cycle_time()); break;
		case 1: draw_model(models.misc.options, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break;
		case 2: draw_model(models.misc.msdos, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break;
	}
}

static void page_main_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "OPTIONS", page_main_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;

	menu_page_add_button(page, 0, "START GAME", button_start_game);
	menu_page_add_button(page, 1, "OPTIONS", button_options);
}

// -----------------------------------------------------------------------------
// Options

static void button_controls(menu_t *menu, int data) {
	page_options_controls_init(menu);
}

static void button_video(menu_t *menu, int data) {
	page_options_video_init(menu);
}

static void button_audio(menu_t *menu, int data) {
	page_options_audio_init(menu);
}

static void button_highscores(menu_t *menu, int data) {
	page_options_highscores_init(menu);
}

static void button_bonus(menu_t *menu, int data) {
	page_options_bonus_init(menu);
}

static void page_options_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(models.controller, vec2(0, -0.1), vec3(0, 0, -6000), system_cycle_time()); break;
		case 1: draw_model(models.rescue, vec2(0, -0.2), vec3(0, 0, -700), system_cycle_time()); break; // TODO: needs better model
		case 2: draw_model(models.options.headphones, vec2(0, -0.2), vec3(0, 0, -300), system_cycle_time()); break;
		case 3: draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		default: break;
	}
}

static void page_options_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "OPTIONS", page_options_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	menu_page_add_button(page, 0, "CONTROLS", button_controls);
	menu_page_add_button(page, 1, "VIDEO", button_video);
	menu_page_add_button(page, 2, "AUDIO", button_audio);
	menu_page_add_button(page, 3, "BEST TIMES", button_highscores);
	menu_page_add_button(page, 4, "BONUS", button_bonus);	
}

// -----------------------------------------------------------------------------
// Options Controls

static int control_current_action;
static float await_input_deadline;

extern int if_to_await;

void button_capture(void *user, button_t button, int32_t ascii_char) {
	if (button == INPUT_INVALID) {
		if_to_await = 0;
		return;
	}

	menu_t *menu = (menu_t *)user;
	if (button == INPUT_KEY_ESCAPE) {
		input_capture(NULL, NULL);
		menu_pop(menu);
		if_to_await = 0;
		return;
	}

	// unbind this button if it's bound anywhere
	for (int i = 0; i < len(save.buttons); i++) {
		if (save.buttons[i][0] == button) {
			save.buttons[i][0] = INPUT_INVALID;
		}
	}
	input_capture(NULL, NULL);
	input_bind(INPUT_LAYER_USER, button, control_current_action);
	save.buttons[control_current_action][0] = button;
	save.is_dirty = true;
	menu_pop(menu);
	if_to_await = 0;
}

static void page_options_control_set_draw(menu_t *menu, int data) {
	float remaining = await_input_deadline - platform_now();
	menu_page_t *page = &menu->pages[menu->index];
	char remaining_text[2] = { '0' + (uint8_t)clamp(remaining + 1, 0, 3), '\0'};
	vec2i_t pos = vec2i(page->items_pos.x, page->items_pos.y + 24);
	ui_draw_text_centered(remaining_text, ui_scaled_pos(page->items_anchor, pos), UI_SIZE_16, UI_COLOR_DEFAULT);

	if (remaining <= 0) {
		input_capture(NULL, NULL);
		menu_pop(menu);
		if_to_await = 0;
		return;
	}
}

static void page_options_controls_set_init(menu_t *menu, int data) {
	control_current_action = data;
	await_input_deadline = platform_now() + 3;
	menu_page_t *page = menu_push(menu, "AWAITING INPUT", page_options_control_set_draw);
	// compiler hush
	(void)page;
	if_to_await = 1;	
	input_capture(button_capture, menu);
}


static void page_options_control_draw(menu_t *menu, int data) {
	menu_page_t *page = &menu->pages[menu->index];

	int left = page->items_pos.x + page->block_width - 100;
	int line_y = page->items_pos.y - 20;

	vec2i_t left_head_pos = vec2i(left - ui_text_width("CONTROLLER", UI_SIZE_8), line_y);
	ui_draw_text("CONTROLLER", ui_scaled_pos(page->items_anchor, left_head_pos), UI_SIZE_8, UI_COLOR_DEFAULT);

	line_y += 20;

	for (int action = 0; action < NUM_GAME_ACTIONS; action++) {
		rgba_t text_color = UI_COLOR_DEFAULT;
		if (action == data) {
			text_color = UI_COLOR_ACCENT;
		}

		if (save.buttons[action][0] != INPUT_INVALID) {
			const char *name = input_button_to_name(save.buttons[action][0]);
			if (!name) {
				name = "UNKNOWN";
			}
			vec2i_t pos = vec2i(left - ui_text_width(name, UI_SIZE_8), line_y);
			ui_draw_text(name, ui_scaled_pos(page->items_anchor, pos), UI_SIZE_8, text_color);
		}
		line_y += 12;
	}
}

static void page_options_controls_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "CONTROLS", page_options_control_draw);
	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -50);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	menu_page_add_button(page, A_UP, "UP", page_options_controls_set_init);
	menu_page_add_button(page, A_DOWN, "DOWN", page_options_controls_set_init);
	menu_page_add_button(page, A_LEFT, "LEFT", page_options_controls_set_init);
	menu_page_add_button(page, A_RIGHT, "RIGHT", page_options_controls_set_init);
	menu_page_add_button(page, A_BRAKE_LEFT, "BRAKE L", page_options_controls_set_init);
	menu_page_add_button(page, A_BRAKE_RIGHT, "BRAKE R", page_options_controls_set_init);
	menu_page_add_button(page, A_THRUST, "THRUST", page_options_controls_set_init);
	menu_page_add_button(page, A_FIRE, "FIRE", page_options_controls_set_init);
	menu_page_add_button(page, A_CHANGE_VIEW, "VIEW", page_options_controls_set_init);
}

static void toggle_rapier(menu_t *menu, int data) {
	save.has_rapier_class = data;
	save.is_dirty = true;
}

static void toggle_bonus(menu_t *menu, int data) {
	save.has_bonus_circuts = data;
	save.is_dirty = true;
}

static const char *opts_off_on[] = {"OFF", "ON"};

// ----
// Options Bonus

static void page_options_bonus_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "BONUS", NULL);
	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -60);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	menu_page_add_toggle(page, save.has_rapier_class, "UNLOCK RAPIER CLASS", opts_off_on, len(opts_off_on), toggle_rapier);
	menu_page_add_toggle(page, save.has_bonus_circuts, "UNLOCK BONUS CIRCUITS", opts_off_on, len(opts_off_on), toggle_bonus);
}

// -----------------------------------------------------------------------------
// Options Video

static void toggle_internal_roll(menu_t *menu, int data) {
	save.internal_roll = (float)data * 0.1;
	save.is_dirty = true;
}

static void toggle_show_fps(menu_t *menu, int data) {
	save.show_fps = data;
	save.is_dirty = true;
}

static void toggle_fade(menu_t *menu, int data) {
	save.fade = data;
	save.is_dirty = true;

	video_probably_slow = 0;
	video_probably_slow |= ((save.render_dist > 0) && (save.fade));
	video_probably_slow |= (((save.render_dist > 3) && !(save.fade)) << 1);
}

static void toggle_filter(menu_t *menu, int data) {
	save.filter = data;
	save.is_dirty = true;
}

static void toggle_ui_scale(menu_t *menu, int data) {
	save.ui_scale = data;
	save.is_dirty = true;
}

extern float RENDERDIST;
extern float RDSQ;
static void toggle_renderdist(menu_t *menu, int data) {
	save.render_dist = data;
	RENDERDIST = RENDER_FADEOUT_FAR + (4000.0f * ((float)data * 10.f / 8.0f));
	RDSQ = RENDERDIST * RENDERDIST;
	render_set_screen_size(platform_screen_size());
	save.is_dirty = true;

	video_probably_slow = 0;
	video_probably_slow |= ((save.render_dist > 0) && (save.fade));
	video_probably_slow |= (((save.render_dist > 3) && !(save.fade)) << 1);
}

#if 0
static void toggle_shading(menu_t *menu, int data) {
	save.shading = data;
	save.is_dirty = true;
}
#endif

static const char *opts_renderdist[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8"};
#if 0
static const char *opts_psx_pc[] = {"PSX", "PC"};
#endif
static const char *opts_roll[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
static const char *opts_ui_sizes[] = {"LOW", "HIGH"};

static void page_options_video_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "VIDEO OPTIONS", (void(*)(menu_t *, int))0xBBBBCCCC);
	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -60);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	menu_page_add_toggle(page, save.internal_roll * 10, "INTERNAL VIEW ROLL", opts_roll, len(opts_roll), toggle_internal_roll);
	menu_page_add_toggle(page, save.ui_scale, "UI SCALE", opts_ui_sizes, len(opts_ui_sizes), toggle_ui_scale);
	menu_page_add_toggle(page, save.show_fps, "SHOW FPS", opts_off_on, len(opts_off_on), toggle_show_fps);
	menu_page_add_toggle(page, save.render_dist, "RENDER DISTANCE", opts_renderdist, len(opts_renderdist), toggle_renderdist);
	menu_page_add_toggle(page, save.fade, "DISTANCE FADE", opts_off_on, len(opts_off_on), toggle_fade);
#if 0
	menu_page_add_toggle(page, save.shading, "SHADING", opts_psx_pc, len(opts_psx_pc), toggle_shading);	
#endif
	menu_page_add_toggle(page, save.filter, "TEXTURE FILTER", opts_off_on, len(opts_off_on), toggle_filter);	
}

// -----------------------------------------------------------------------------
// Options Audio

extern void wav_volume(int volume);

static void toggle_music_volume(menu_t *menu, int data) {
	save.music_volume = (float)data * 0.1;
	save.is_dirty = true;
	wav_volume(192 * save.music_volume);
}

static void toggle_sfx_volume(menu_t *menu, int data) {
	save.sfx_volume = (float)data * 0.1;	
	save.is_dirty = true;
}

static const char *opts_volume[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};

static void page_options_audio_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "AUDIO OPTIONS", NULL);

	flags_set(page->layout_flags, MENU_VERTICAL | MENU_FIXED);
	page->title_pos = vec2i(-160, -100);
	page->title_anchor = UI_POS_MIDDLE | UI_POS_CENTER;
	page->items_pos = vec2i(-160, -80);
	page->block_width = 320;
	page->items_anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	menu_page_add_toggle(page, save.music_volume * 10, "MUSIC VOLUME", opts_volume, len(opts_volume), toggle_music_volume);
	menu_page_add_toggle(page, save.sfx_volume * 10, "SOUND EFFECTS VOLUME", opts_volume, len(opts_volume), toggle_sfx_volume);
}

// -----------------------------------------------------------------------------
// Options Best Times

static int options_highscores_race_class;
static int options_highscores_circut;
static int options_highscores_tab;

static void page_options_highscores_viewer_input_handler() {
	int last_race_class_index = options_highscores_race_class;
	int last_circut_index = options_highscores_circut;

	if (input_pressed(A_MENU_UP)) {
		options_highscores_race_class--;
	}
	else if (input_pressed(A_MENU_DOWN)) {
		options_highscores_race_class++;
	}

	if (input_pressed(A_MENU_LEFT)) {
		options_highscores_circut--;
	}
	else if (input_pressed(A_MENU_RIGHT)) {
		options_highscores_circut++;
	}

	if (options_highscores_race_class >= NUM_RACE_CLASSES) {
		options_highscores_race_class = 0;
	}
	if (options_highscores_race_class < 0) {
		options_highscores_race_class = NUM_RACE_CLASSES - 1;
	}

	if (options_highscores_circut >= NUM_CIRCUTS) {
		options_highscores_circut = 0;
	}
	if (options_highscores_circut < 0) {
		options_highscores_circut = NUM_CIRCUTS - 1;
	}

	if ((last_race_class_index != options_highscores_race_class) ||
		(last_circut_index != options_highscores_circut)) {
		sfx_play(SFX_MENU_MOVE);
	}
}

static void page_options_highscores_viewer_draw(menu_t *menu, int data) {
	ui_pos_t anchor = UI_POS_MIDDLE | UI_POS_CENTER;

	vec2i_t pos = vec2i(0, -70);
	ui_draw_text_centered(def.race_classes[options_highscores_race_class].name, ui_scaled_pos(anchor, pos), UI_SIZE_12, UI_COLOR_DEFAULT);
	pos.y += 16;
	ui_draw_text_centered(def.circuts[options_highscores_circut].name, ui_scaled_pos(anchor, pos), UI_SIZE_12, UI_COLOR_ACCENT);
	
	vec2i_t entry_pos = vec2i(pos.x - 110, pos.y + 24);
	highscores_t *hs = &save.highscores[options_highscores_race_class][options_highscores_circut][options_highscores_tab];
	for (int i = 0; i < NUM_HIGHSCORES; i++) {
		ui_draw_text(hs->entries[i].name, ui_scaled_pos(anchor, entry_pos), UI_SIZE_16, UI_COLOR_DEFAULT);
		ui_draw_time(hs->entries[i].time, ui_scaled_pos(anchor, vec2i(entry_pos.x + 110, entry_pos.y)), UI_SIZE_16, UI_COLOR_DEFAULT);
		entry_pos.y += 24;
	}

	vec2i_t lap_pos = vec2i(entry_pos.x - 40, entry_pos.y + 8);
	ui_draw_text("LAP RECORD", ui_scaled_pos(anchor, lap_pos), UI_SIZE_12, UI_COLOR_ACCENT);
	ui_draw_time(hs->lap_record, ui_scaled_pos(anchor, vec2i(lap_pos.x + 180, lap_pos.y - 4)), UI_SIZE_16, UI_COLOR_DEFAULT);

	page_options_highscores_viewer_input_handler();
}

static void page_options_highscores_viewer_init(menu_t *menu) {
	menu_page_t *page;
	if (options_highscores_tab == HIGHSCORE_TAB_TIME_TRIAL) {
		page = menu_push(menu, "BEST TIME TRIAL TIMES", page_options_highscores_viewer_draw);
	}
	else /*options_highscores_tab == HIGHSCORE_TAB_RACE)*/ {
		page = menu_push(menu, "BEST RACE TIMES", page_options_highscores_viewer_draw);
	}

	flags_add(page->layout_flags, MENU_FIXED);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->title_pos = vec2i(0, 30);
}

static void button_highscores_viewer(menu_t *menu, int data) {
	options_highscores_tab = data;
	page_options_highscores_viewer_init(menu);
}

static void page_options_highscores_draw(menu_t *menu, int data) {
	draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time());
}

static void page_options_highscores_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "VIEW BEST TIMES", page_options_highscores_draw);

	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;

	options_highscores_race_class = RACE_CLASS_VENOM;
	options_highscores_circut = CIRCUT_ALTIMA_VII;

	menu_page_add_button(page, HIGHSCORE_TAB_TIME_TRIAL, "TIME TRIAL TIMES", button_highscores_viewer);
	menu_page_add_button(page, HIGHSCORE_TAB_RACE, "RACE TIMES", button_highscores_viewer);
}



// -----------------------------------------------------------------------------
// Racing class

static void button_race_class_select(menu_t *menu, int data) {
	if (!save.has_rapier_class && data == RACE_CLASS_RAPIER) {
		return;
	}
	g.race_class = data;
	page_race_type_init(menu);
}

static void page_race_class_draw(menu_t *menu, int data) {
	menu_page_t *page = &menu->pages[menu->index];
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	draw_model(models.race_classes[data], vec2(0, -0.2), vec3(0, 0, -350), system_cycle_time());

	if (!save.has_rapier_class && data == RACE_CLASS_RAPIER) {
		render_set_view_2d();
		vec2i_t pos = vec2i(page->items_pos.x, page->items_pos.y + 32);
		ui_draw_text_centered("NOT AVAILABLE", ui_scaled_pos(page->items_anchor, pos), UI_SIZE_12, UI_COLOR_ACCENT);
	}
}

static void page_race_class_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACING CLASS", page_race_class_draw);
	for (int i = 0; i < len(def.race_classes); i++) {
		menu_page_add_button(page, i, def.race_classes[i].name, button_race_class_select);
	}
}



// -----------------------------------------------------------------------------
// Race Type

static void button_race_type_select(menu_t *menu, int data) {
	g.race_type = data;
	g.highscore_tab = g.race_type == RACE_TYPE_TIME_TRIAL ? HIGHSCORE_TAB_TIME_TRIAL : HIGHSCORE_TAB_RACE;
	page_team_init(menu);
}

static void page_race_type_draw(menu_t *menu, int data) {
	switch (data) {
		case 0: draw_model(models.misc.championship, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		case 1: draw_model(models.misc.single_race, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
		case 2: draw_model(models.options.stopwatch, vec2(0, -0.2), vec3(0, 0, -400), system_cycle_time()); break;
	}
}

static void page_race_type_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACE TYPE", page_race_type_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.race_types); i++) {
		menu_page_add_button(page, i, def.race_types[i].name, button_race_type_select);
	}
}



// -----------------------------------------------------------------------------
// Team

static void button_team_select(menu_t *menu, int data) {
	g.team = data;
	page_pilot_init(menu);
}

static void page_team_draw(menu_t *menu, int data) {
	int team_model_index = (data + 3) % 4; // models in the prm are shifted by -1
	draw_model(models.teams[team_model_index], vec2(0, -0.2), vec3(0, 0, -10000), system_cycle_time());
	draw_model(g.ships[def.teams[data].pilots[0]].model, vec2(0, -0.3), vec3(-700, -800, -1300), system_cycle_time()*1.1);
	draw_model(g.ships[def.teams[data].pilots[1]].model, vec2(0, -0.3), vec3( 700, -800, -1300), system_cycle_time()*1.2);
}

static void page_team_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT YOUR TEAM", page_team_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.teams); i++) {
		menu_page_add_button(page, i, def.teams[i].name, button_team_select);
	}
}



// -----------------------------------------------------------------------------
// Pilot

static void button_pilot_select(menu_t *menu, int data) {
	g.pilot = data;
	if (g.race_type != RACE_TYPE_CHAMPIONSHIP) {
		page_circut_init(menu);
	}
	else {
		in_menu = 0;
		g.circut = 0;
		game_reset_championship();
		game_set_scene(GAME_SCENE_RACE);
	}
}

static void page_pilot_draw(menu_t *menu, int data) {
	draw_model(models.pilots[def.pilots[data].logo_model], vec2(0, -0.2), vec3(0, 0, -10000), system_cycle_time());
}

static void page_pilot_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "CHOOSE YOUR PILOT", page_pilot_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -110);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.teams[g.team].pilots); i++) {
		menu_page_add_button(page, def.teams[g.team].pilots[i], def.pilots[def.teams[g.team].pilots[i]].name, button_pilot_select);
	}
}


// -----------------------------------------------------------------------------
// Circut
extern void wav_volume(int vol);
static void button_circut_select(menu_t *menu, int data) {
	in_menu = 0;
	g.circut = data;
//	wav_volume(0);//255 * save.music_volume);

	game_set_scene(GAME_SCENE_RACE);
}

static void page_circut_draw(menu_t *menu, int data) {
	vec2i_t pos = vec2i(0, -25);
	vec2i_t size = vec2i(128, 74);
	vec2i_t scaled_size = ui_scaled(size);
	vec2i_t scaled_pos = ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(pos.x - size.x/2, pos.y - size.y/2));
	render_push_2d(scaled_pos, scaled_size, rgba(128, 128, 128, 255), texture_from_list(track_images, data));
}

static void page_circut_init(menu_t *menu) {
	menu_page_t *page = menu_push(menu, "SELECT RACING CIRCUT", page_circut_draw);
	flags_add(page->layout_flags, MENU_FIXED);
	page->title_pos = vec2i(0, 30);
	page->title_anchor = UI_POS_TOP | UI_POS_CENTER;
	page->items_pos = vec2i(0, -100);
	page->items_anchor = UI_POS_BOTTOM | UI_POS_CENTER;
	for (int i = 0; i < len(def.circuts); i++) {
		if (!def.circuts[i].is_bonus_circut || save.has_bonus_circuts) {
			menu_page_add_button(page, i, def.circuts[i].name, button_circut_select);
		}
	}
}

#define objects_unpack(DEST, SRC) \
	objects_unpack_imp((Object **)&DEST, sizeof(DEST)/sizeof(Object*), SRC)

static void objects_unpack_imp(Object **dest_array, int len, Object *src) {
	int i;
	for (i = 0; src && i < len; i++) {
		dest_array[i] = src;
		src = src->next;
	}
	error_if(i != len, "expected %d models got %d", len, i)
}

extern int load_OP;
extern int LOAD_UNFILTERED;

void main_menu_init(void) {
	in_menu = 1;
	g.is_attract_mode = false;
//	wav_volume(192 * save.music_volume);		
	ships_reset_exhaust_plumes();

	main_menu = mem_bump(sizeof(menu_t));

	load_OP = 1;
	background = image_get_texture("wipeout/textures/wipeout1.tim");
	load_OP = 0;
	track_images = image_get_compressed_textures("wipeout/textures/track.cmp");

LOAD_UNFILTERED = 1;
	objects_unpack(models.race_classes, objects_load("wipeout/common/leeg.prm", image_get_compressed_textures("wipeout/common/leeg.cmp")));
	objects_unpack(models.teams, objects_load("wipeout/common/teams.prm", texture_list_empty()));
LOAD_UNFILTERED = 0;
	objects_unpack(models.pilots, objects_load("wipeout/common/pilot.prm", image_get_compressed_textures("wipeout/common/pilot.cmp")));
LOAD_UNFILTERED = 1;
	objects_unpack(models.options, objects_load("wipeout/common/alopt.prm", image_get_compressed_textures("wipeout/common/alopt.cmp")));
LOAD_UNFILTERED = 0;
	objects_unpack(models.rescue, objects_load("wipeout/common/rescu.prm", image_get_compressed_textures("wipeout/common/rescu.cmp")));
LOAD_UNFILTERED = 1;
	objects_unpack(models.controller, objects_load("wipeout/common/pad1.prm", image_get_compressed_textures("wipeout/common/pad1.cmp")));
	objects_unpack(models.misc, objects_load("wipeout/common/msdos.prm", image_get_compressed_textures("wipeout/common/msdos.cmp")));
LOAD_UNFILTERED = 0;
	menu_reset(main_menu);
	page_main_init(main_menu);
}
extern pvr_dr_state_t dr_state;
void main_menu_update(void) {
	render_set_view_2d();
	render_set_depth_write(false);
	render_push_2d(vec2i(0, 0), render_size(), rgba(128, 128, 128, 255), background);
	render_set_depth_write(true);
	pvr_list_finish();
	pvr_list_begin(PVR_LIST_TR_POLY);
	pvr_dr_init(&dr_state);

	menu_update(main_menu);
}

