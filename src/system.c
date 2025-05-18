#include "system.h"
#include "input.h"
#include "render.h"
#include "platform.h"
#include "mem.h"
#include "utils.h"

#include "wipeout/game.h"
#include "kos.h"

uint8_t allow_exit;

static float time_real;
static float time_scaled;
static float time_scale = 1.0;
static float tick_last;
static float cycle_time = 0;

void system_init(void) {
	time_real = platform_now();

	input_init();
	render_init();
	game_init();
}

void system_cleanup(void) {
	render_cleanup();
	input_cleanup();
}

void system_exit(void) {
	platform_exit();
}

void system_update(void) {
	float time_real_now = platform_now();
	float real_delta = time_real_now - time_real;
	time_real = time_real_now;
	tick_last = fminf(real_delta, 0.1f) * time_scale;
	time_scaled += tick_last;

	// FIXME: come up with a better way to wrap the cycle_time, so that it
	// doesn't lose precission, but also doesn't jump upon reset.
	cycle_time = time_scaled;
	if (cycle_time > 3600 * F_PI) {
		cycle_time -= 3600 * F_PI;
	}

	render_frame_prepare();

	game_update();

	render_frame_end();

	input_clear();
}

void system_reset_cycle_time(void) {
	cycle_time = 0;
}

void system_resize(vec2i_t size) {
	; //
}

float system_time_scale_get(void) {
	return time_scale;
}

void system_time_scale_set(float scale) {
	time_scale = scale;
}

float system_tick(void) {
	return tick_last;
}

float system_time(void) {
	return time_scaled;
}

float system_cycle_time(void) {
	return cycle_time;
}
