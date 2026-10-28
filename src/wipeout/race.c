#include "../mem.h"
#include "../input.h"
#include "../platform.h"
#include "../system.h"
#include "../utils.h"

#include "object.h"
#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "droid.h"
#include "camera.h"
#include "object.h"
#include "scene.h"
#include "game.h"
#include "hud.h"
#include "sfx.h"
#include "race.h"
#include "particle.h"
#include "menu.h"
#include "ship_ai.h"

// While there are no menus to choose the number of players, every extra
// controller plugged in takes over one more ship (up to MULTIPLAYER_MAX_PLAYERS).
// 1: ignore the menu, every controller plugged in is a player (for testing)
// 0: the number of players comes from the menu
#ifndef MULTIPLAYER_TEST
#define MULTIPLAYER_TEST 0
#endif
#ifndef MULTIPLAYER_MAX_PLAYERS
#define MULTIPLAYER_MAX_PLAYERS 4
#endif
#include "ingame_menus.h"

#define ATTRACT_DURATION 60.0


static bool is_paused = false;
static bool menu_is_scroll_text = false;
static int players_finished = 0; // bit mask, one bit per human player
static float attract_start_time;
static menu_t *active_menu = NULL;
extern volatile uint32_t last_five_tracks[5];
void race_init(void) {
	ingame_menus_load();
	menu_is_scroll_text = false;

	const circut_settings_t *cs = &def.circuts[g.circut].settings[g.race_class];
	track_load(cs->path);
	scene_load(cs->path, cs->sky_y_offset);

	if (g.circut == CIRCUT_SILVERSTREAM && g.race_class == RACE_CLASS_RAPIER) {
		scene_init_aurora_borealis();
	}

	if (g.is_attract_mode) {
		g.pilot = rand_int(0, len(def.pilots));
	}
	race_start();

	if (g.is_attract_mode) {
		attract_start_time = system_time();

		for (int i = 0; i < len(g.ships); i++) {
			flags_rm(g.ships[i].flags, SHIP_VIEW_INTERNAL);
			flags_rm(g.ships[i].flags, SHIP_RACING);
		}

		g.camera.update_func = camera_update_attract_random;
		if ((rand_int(0, 257) % 3) == 0) {
			active_menu = text_scroll_menu_init(def.credits, len(def.credits));
			menu_is_scroll_text = true;
		}
	}

	is_paused = false;

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
		last_five_tracks[i-1] = last_five_tracks[i];
	}

	last_five_tracks[4] = new_index;

	sfx_music_mode(SFX_MUSIC_SCENECHANGE);
	sfx_music_play(new_index);
}

extern int sprites_to_draw;
void draw_all_sprites(void);
extern void ShakeScreen(void);

// Split screen layout. Every viewport is aligned on 32 pixels tiles (the
// hardware clips on tiles), with a black band between viewports:
// 2 players: two strips of 640x224, 4 players: four quarters of 320x224.
static void race_viewport_rect(int player, int *x, int *y, int *w, int *h) {
	if (g.num_players == 2) {
		*x = 0;
		*y = (player == 0) ? 0 : 256;
		*w = 640;
		*h = 224;
	}
	else {
		*x = (player & 1) ? 320 : 0;
		*y = (player & 2) ? 256 : 0;
		*w = 320;
		*h = 224;
	}
}

static void race_draw_split(void) {
	int x, y, w, h;

	// Pass 1, opaque list: the sky of every viewport
	for (int p = 0; p < g.num_players; p++) {
		race_viewport_rect(p, &x, &y, &w, &h);
		render_set_viewport(x, y, w, h);

		camera_t *camera = camera_of_player(p);
		render_set_view(camera->position, camera->angle);
		scene_draw_sky(camera);
	}

	// Pass 2, transparent list: everything else, viewport by viewport
	render_begin_transparent_list();

	int saved_ui_scale = ui_get_scale();
	for (int p = 0; p < g.num_players; p++) {
		race_viewport_rect(p, &x, &y, &w, &h);
		render_set_viewport(x, y, w, h);

		camera_t *camera = camera_of_player(p);
		g.render_pilot = g.human_pilot[p];

		render_set_view(camera->position, camera->angle);

		scene_draw_objects(camera);

		render_set_cull_backface(false);
		track_draw(camera);
		render_set_cull_backface(true);

		ships_draw();
		for (int d = 0; d < g.num_players; d++) {
			droid_draw(droid_of_player(d));
		}

		weapons_draw();
		particles_draw();

		// deferred trees and other things
		if (sprites_to_draw) {
			render_set_depth_write(false);
			draw_all_sprites();
			render_set_depth_write(true);
		}

		// Draw 2d, in the viewport
		render_set_view_2d();
		ui_set_scale(1);
		if (flags_is(g.ships[g.render_pilot].flags, SHIP_RACING)) {
			hud_draw(&g.ships[g.render_pilot]);
		}
		ui_set_scale(saved_ui_scale);
	}

	// Menus use the whole screen
	render_reset_viewport();
	g.render_pilot = g.pilot;
	render_set_view_2d();

	if (active_menu) {
		if (!menu_is_scroll_text) {
			vec2i_t size = render_size();
			render_push_2d(vec2i(0, 0), size, rgba(0, 0, 0, 128), RENDER_NO_TEXTURE);
		}
		menu_update(active_menu);
	}
}

void race_update(void) {
	if (is_paused) {
		if (!active_menu) {
			active_menu = pause_menu_init();
		}
		if (input_pressed(A_MENU_QUIT)) {
			race_unpause();
		}
	}
	else {
		ships_update();
		for (int p = 0; p < g.num_players; p++) {
			ship_t *player_ship = &g.ships[g.human_pilot[p]];
			droid_update(droid_of_player(p), player_ship);
			camera_update(camera_of_player(p), player_ship, droid_of_player(p));
		}
		ShakeScreen();
		weapons_update();
		particles_update();
		scene_update();
		if (g.race_type != RACE_TYPE_TIME_TRIAL) {
			track_cycle_pickups();
		}

		if (g.is_attract_mode) {
			if (input_pressed(A_MENU_START) || input_pressed(A_MENU_SELECT)) {
				render_state.in_race = 0;
				sfx_music_pause();
				game_set_scene(GAME_SCENE_MAIN_MENU);
			}
			float duration = system_time() - attract_start_time;
			float credwait = 85;
			if (ui_get_scale() == 1)
				credwait = 90;

			if ((!active_menu && duration > 35) || duration > credwait) {
				render_state.in_race = 0;
				sfx_music_pause();
				game_set_scene(GAME_SCENE_TITLE);
			}
		}
		else if (active_menu == NULL && (input_pressed(A_MENU_START) || input_pressed(A_MENU_QUIT))) {
			race_pause();
		}
	}

	g.render_pilot = g.pilot;
	if (g.num_players > 1) {
		race_draw_split();
		return;
	}

	// Draw 3D
	render_set_view(g.camera.position, g.camera.angle);

	scene_draw(&g.camera);

	render_set_cull_backface(false);
	track_draw(&g.camera);
	render_set_cull_backface(true);

	// fewer cache misses when disabling interrupts around some draw calls
//	irq_disable();
	ships_draw();
	droid_draw(&g.droid);

	weapons_draw();
	particles_draw();
//	irq_enable();

	// deferred trees and other things
 	if (sprites_to_draw) {
		render_set_depth_write(false);
		draw_all_sprites();
		render_set_depth_write(true);
	}

	// Draw 2d
	render_set_view_2d();

	if (g.is_attract_mode && !active_menu) {
		ui_draw_text("DEMO MODE", ui_scaled_pos(UI_POS_TOP | UI_POS_CENTER, vec2i(-56, 24)), UI_SIZE_8, UI_COLOR_ACCENT);
	}

	if (flags_is(g.ships[g.pilot].flags, SHIP_RACING)) {
		hud_draw(&g.ships[g.pilot]);
	}

	if (active_menu) {
		if (!menu_is_scroll_text) {
			vec2i_t size = render_size();
			render_push_2d(vec2i(0, 0), size, rgba(0, 0, 0, 128), RENDER_NO_TEXTURE);
		}
		menu_update(active_menu);
	}
}

void race_start(void) {
	// Human players. Player 0 is the one who picked a ship in the menus.
	g.num_players = 1;
	g.human_pilot[0] = g.pilot;
	if (!g.is_attract_mode && g.race_type != RACE_TYPE_TIME_TRIAL) {
		int wanted = g.players_chosen;
#if MULTIPLAYER_TEST
		wanted = MULTIPLAYER_MAX_PLAYERS;
#endif
		// never more players than controllers plugged in
		int controllers = platform_num_controllers();
		if (wanted > controllers) {
			wanted = controllers;
		}
		if (wanted > MULTIPLAYER_MAX_PLAYERS) {
			wanted = MULTIPLAYER_MAX_PLAYERS;
		}
		if (g.race_type != RACE_TYPE_SINGLE && !MULTIPLAYER_TEST) {
			wanted = 1;
		}
		for (int k = 1; k < NUM_PILOTS && g.num_players < wanted; k++) {
			g.human_pilot[g.num_players++] = (g.pilot + k) % NUM_PILOTS;
		}
	}

	render_state.in_race = 1;
	active_menu = NULL;
	sfx_reset();
	scene_init();
	if (g.circut == CIRCUT_TERRAMAX) {
		render_reset_proj(256000.0f);
	} else if (g.circut == CIRCUT_ALTIMA_VII) {
		render_reset_proj(128000.0f);
	} else {
		render_reset_proj(96000.0f);
	}
	g.render_pilot = g.pilot;
	players_finished = 0;
	for (int p = 0; p < g.num_players; p++) {
		camera_init(camera_of_player(p), g.track.sections);
		camera_of_player(p)->update_func = camera_update_race_intro;
	}
	ships_init(g.track.sections);
	for (int p = 0; p < g.num_players; p++) {
		droid_init(droid_of_player(p), &g.ships[g.human_pilot[p]]);
	}
	particles_init();
	weapons_init();

	for (int i = 0; i < len(g.race_ranks); i++) {
		g.race_ranks[i].points = 0;
		g.race_ranks[i].pilot = i;
	}
	for (int i = 0; i < len(g.lap_times); i++) {
		for (int j = 0; j < len(g.lap_times[i]); j++) {
			g.lap_times[i][j] = 0;
		}
	}
	g.is_new_race_record = false;
	g.is_new_lap_record = false;
	g.best_lap = 0;
	g.race_time = 0;
}

void race_restart(void) {
	race_unpause();

	if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		g.lives--;
		if (g.lives == 0) {
			race_release_control();
			active_menu = game_over_menu_init();
			return;
		}
	}

	race_start();
}

static bool sort_points_compare(pilot_points_t *pa, pilot_points_t *pb) {
	return (pa->points < pb->points);
}

// Single player: same as race_end(). Several players: a player who finishes
// gets his ship released (autopilot), the race ends when all of them finished.
void race_player_finished(int player) {
	if (g.num_players <= 1) {
		race_end();
		return;
	}

	players_finished |= (1 << player);

	ship_t *ship = &g.ships[g.human_pilot[player]];
	flags_rm(ship->flags, SHIP_RACING);
	ship->remote_thrust_max = 3160;
	ship->remote_thrust_mag = 32;
	ship->speed = 3160;
	camera_of_player(player)->update_func = camera_update_attract_random;

	if (players_finished == ((1 << g.num_players) - 1)) {
		race_end();
	}
}

void race_end(void) {
	race_release_control();

	g.race_position = g.ships[g.pilot].position_rank;

	g.race_time = 0;
	g.best_lap = g.lap_times[g.pilot][0];
	for (int i = 0; i < NUM_LAPS; i++) {
		g.race_time += g.lap_times[g.pilot][i];
		if (g.lap_times[g.pilot][i] < g.best_lap) {
			g.best_lap = g.lap_times[g.pilot][i];
		}
	}

	highscores_t *hs = &save.highscores[g.race_class][g.circut][g.highscore_tab];
	if (g.best_lap < hs->lap_record) {
		hs->lap_record = g.best_lap;
		g.is_new_lap_record = true;
		save.is_dirty = true;
	}

	for (int i = 0; i < NUM_HIGHSCORES; i++) {
		if (g.race_time < hs->entries[i].time) {
			g.is_new_race_record = true;
			break;
		}
	}

	if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		for (int i = 0; i < len(def.race_points_for_rank); i++) {
			g.race_ranks[i].points = def.race_points_for_rank[i];

			// Find the pilot for this race rank in the championship table
			for (int j = 0; j < len(g.championship_ranks); j++) {
				if (g.race_ranks[i].pilot == g.championship_ranks[j].pilot) {
					g.championship_ranks[j].points += def.race_points_for_rank[i];
					break;
				}
			}
		}
		sort(g.championship_ranks, len(g.championship_ranks), sort_points_compare);
	}

	active_menu = race_stats_menu_init();
}

void race_next(void) {
	int next_circut = g.circut + 1;

	// Championship complete
	if (
		(save.has_bonus_circuts && next_circut >= NUM_CIRCUTS) ||
		(!save.has_bonus_circuts && next_circut >= NUM_NON_BONUS_CIRCUTS)
	) {
		if (g.race_class == RACE_CLASS_RAPIER) {
			if (save.has_bonus_circuts) {
				active_menu = text_scroll_menu_init(def.congratulations.rapier_all_circuts, len(def.congratulations.rapier_all_circuts));
			}
			else {
				save.has_bonus_circuts = true;
				active_menu = text_scroll_menu_init(def.congratulations.rapier, len(def.congratulations.rapier));
			}
		}
		else {
			save.has_rapier_class = true;
			if (save.has_bonus_circuts) {
				active_menu = text_scroll_menu_init(def.congratulations.venom_all_circuts, len(def.congratulations.venom_all_circuts));
			}
			else {
				active_menu = text_scroll_menu_init(def.congratulations.venom, len(def.congratulations.venom));
			}
		}
		save.is_dirty = true;
		menu_is_scroll_text = true;
	}

	// Next track
	else {
		g.circut = next_circut;
		sfx_music_pause();
		game_set_scene(GAME_SCENE_RACE);
	}
}

void race_release_control(void) {
	flags_rm(g.ships[g.pilot].flags, SHIP_RACING);
	g.ships[g.pilot].remote_thrust_max = 3160;
	g.ships[g.pilot].remote_thrust_mag = 32;
	g.ships[g.pilot].speed = 3160;
	g.camera.update_func = camera_update_attract_random;
}

void race_pause(void) {
	sfx_pause();
	is_paused = true;
}

void race_unpause(void) {
	sfx_unpause();
	is_paused = false;
	active_menu = NULL;
}
