#include "../types.h"
#include "../mem.h"
#include "../utils.h"
#include "../system.h"
#include "../platform.h"

#include "object.h"
#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "hud.h"
#include "droid.h"
#include "camera.h"
#include "image.h"
#include "ship_ai.h"
#include "game.h"
#include "ui.h"

static texture_list_t weapon_icon_textures;
static uint16_t target_reticle;

extern uint16_t HUD_NO_TEXTURE;

typedef struct {
	vec2i_t offset;
	uint16_t height;
	rgba_t color;
} speedo_bar_t;

const struct {
	uint16_t width;
	uint16_t skew;
	speedo_bar_t bars[13];
} speedo = {
	.width = 121,
	.skew = 2,
	.bars = {
		{{.x =   6, .y = 12}, .height = 10, .color = rgba( 66,  16,  49, 255)},
		{{.x =  13, .y = 12}, .height = 10, .color = rgba(115,  33,  90, 255)},
		{{.x =  20, .y = 12}, .height = 10, .color = rgba(132,  58, 164, 255)},
		{{.x =  27, .y = 12}, .height = 10, .color = rgba( 99,  90, 197, 255)},
		{{.x =  34, .y = 12}, .height = 10, .color = rgba( 74, 148, 181, 255)},
		{{.x =  41, .y = 12}, .height = 10, .color = rgba( 66, 173, 115, 255)},
		{{.x =  50, .y = 10}, .height = 12, .color = rgba( 99, 206,  58, 255)},
		{{.x =  59, .y =  8}, .height = 12, .color = rgba(189, 206,  41, 255)},
		{{.x =  69, .y =  5}, .height = 13, .color = rgba(247, 140,  33, 255)},
		{{.x =  81, .y =  2}, .height = 15, .color = rgba(255, 197,  49, 255)},
		{{.x =  95, .y =  1}, .height = 16, .color = rgba(255, 222, 115, 255)},
		{{.x = 110, .y =  1}, .height = 16, .color = rgba(255, 239, 181, 255)},
		{{.x = 126, .y =  1}, .height = 16, .color = rgba(255, 255, 255, 255)}
	}
};

static uint16_t speedo_facia_texture;

extern global_render_state_t render_state;

void hud_load(void) {
	render_state.LOAD_UNFILTERED = 1;
	speedo_facia_texture = image_get_texture("wipeout/textures/speedo.tim");
	target_reticle = image_get_texture_semi_trans("wipeout/textures/target2.tim");
	weapon_icon_textures = image_get_compressed_textures("wipeout/common/wicons.cmp");
	render_state.LOAD_UNFILTERED = 0;
}

#include <kos.h>
extern pvr_vertex_t vs[5];

static void hud_draw_speedo_bar(vec2i_t *pos, const speedo_bar_t *a, const speedo_bar_t *b, float f, rgba_t color_override) {
	rgba_t left_color, right_color;
	if (color_override.a > 0) {
		left_color = color_override;
		right_color = color_override;
	}
	else {
		left_color = a->color;
		right_color = rgba(
			lerp(a->color.r, b->color.r, f),
			lerp(a->color.g, b->color.g, f),
			lerp(a->color.b, b->color.b, f),
			lerp(a->color.a, b->color.a, f)
		);
	}


	float right_h = lerp(a->height, b->height, f);
	vec2i_t top_left     = vec2i(a->offset.x + 1, a->offset.y);
	vec2i_t bottom_left  = vec2i(a->offset.x + 1 - a->height / speedo.skew, a->offset.y + a->height);
	vec2i_t top_right    = vec2i(lerp(a->offset.x + 1, b->offset.x, f), lerp(a->offset.y, b->offset.y, f));
	vec2i_t bottom_right = vec2i(top_right.x - right_h / speedo.skew, top_right.y + right_h);

	top_left     = ui_scaled(top_left);
	bottom_left  = ui_scaled(bottom_left);
	top_right    = ui_scaled(top_right);
	bottom_right = ui_scaled(bottom_right);

	uint32_t rawlcol = (left_color.r << 24) | (left_color.g << 16) | (left_color.b << 8) | (left_color.a);
	uint32_t rawrcol = (right_color.r << 24) | (right_color.g << 16) | (right_color.b << 8) | (right_color.a);
	uint32_t lcol,rcol;

	lcol = argb_from_u32_usealpha(rawlcol);
	rcol = argb_from_u32_usealpha(rawrcol);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = pos->x + bottom_left.x;
	vs[0].y = pos->y + bottom_left.y;
	vs[0].z = 0;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = pos->x + top_left.x;
	vs[1].y = pos->y + top_left.y;
	vs[1].z = 0;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;
	vs[2].x = pos->x + bottom_right.x;
	vs[2].y = pos->y + bottom_right.y;
	vs[2].z = 0;
	vs[2].argb = rcol;
	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = pos->x + top_right.x;
	vs[3].y = pos->y + top_right.y;
	vs[3].z = 0;
	vs[3].argb = rcol;
	vs[3].oargb = 0;

	render_hud_quad(HUD_NO_TEXTURE);
}

static void hud_draw_speedo_bars(vec2i_t *pos, float f, rgba_t color_override) {
	if (f <= 0) {
		return;
	}

	if (f - floor(f) > 0.9) {
		f = ceil(f);
	}
	if (f > 13) {
		f = 13;
	}

	int bars = f;
	for (int i = 1; i < bars; i++) {
		hud_draw_speedo_bar(pos, &speedo.bars[i - 1], &speedo.bars[i], 1, color_override);
	}

	if (bars > 12) {
		return;
	}

	float last_bar_fraction = f - bars + 0.1;
	if (last_bar_fraction <= 0) {
		return;
	}

	if (last_bar_fraction > 1) {
		last_bar_fraction = 1;
	}
	int last_bar = bars == 0 ? 1 : bars;
	hud_draw_speedo_bar(pos, &speedo.bars[last_bar - 1], &speedo.bars[last_bar], last_bar_fraction, color_override);
}

static void hud_draw_speedo(int speed, int thrust) {
	vec2i_t facia_pos;
	vec2i_t bar_pos;
	if (platform_screen_size().y == 360) {
		facia_pos = ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-141, -40));
		bar_pos = ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-141, -35));
	} else {
		facia_pos = ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-141, -45));
		bar_pos = ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-141, -40));
	}

	hud_draw_speedo_bars(&bar_pos, thrust / 65.0, rgba(255, 0, 0, 128));
	hud_draw_speedo_bars(&bar_pos, speed / 2166.0, rgba(0, 0, 0, 0));
	render_push_2d(facia_pos, ui_scaled(render_texture_size(speedo_facia_texture)), rgba(255, 255, 255, 255), speedo_facia_texture);
}

extern mat4_t __attribute__((aligned(32))) view_mat;
extern mat4_t __attribute__((aligned(32))) mvp_mat;
extern mat4_t __attribute__((aligned(32))) vp_mat;

// thanks @FalcoGirgis
inline static void mat_load_apply(const matrix_t* matrix1, const matrix_t* matrix2) {
    unsigned int prefetch_scratch;

    asm volatile (
        "mov %[bmtrx], %[pref_scratch]\n\t"
        "add #32, %[pref_scratch]\n\t"
        "fschg\n\t"
        "pref @%[pref_scratch]\n\t"
        // back matrix
        "fmov.d @%[bmtrx]+, XD0\n\t" 
        "fmov.d @%[bmtrx]+, XD2\n\t"
        "fmov.d @%[bmtrx]+, XD4\n\t"
        "fmov.d @%[bmtrx]+, XD6\n\t"
        "pref @%[fmtrx]\n\t"
        "fmov.d @%[bmtrx]+, XD8\n\t" 
        "fmov.d @%[bmtrx]+, XD10\n\t"
        "fmov.d @%[bmtrx]+, XD12\n\t"
        "mov %[fmtrx], %[pref_scratch]\n\t"
        "add #32, %[pref_scratch]\n\t"
        "fmov.d @%[bmtrx], XD14\n\t"
        "pref @%[pref_scratch]\n\t"
        // front matrix
        // interleave loads and matrix multiply 4x4
        "fmov.d @%[fmtrx]+, DR0\n\t"
        "fmov.d @%[fmtrx]+, DR2\n\t"
        "fmov.d @%[fmtrx]+, DR4\n\t"
        "ftrv XMTRX, FV0\n\t"

        "fmov.d @%[fmtrx]+, DR6\n\t"
        "fmov.d @%[fmtrx]+, DR8\n\t"
        "ftrv XMTRX, FV4\n\t"

        "fmov.d @%[fmtrx]+, DR10\n\t"
        "fmov.d @%[fmtrx]+, DR12\n\t"
        "ftrv XMTRX, FV8\n\t"

        "fmov.d @%[fmtrx], DR14\n\t"
        "fschg\n\t"
        "ftrv XMTRX, FV12\n\t"
        "frchg\n"
        : [bmtrx] "+&r" ((unsigned int)matrix1), [fmtrx] "+r" ((unsigned int)matrix2), [pref_scratch] "=&r" (prefetch_scratch)
        : // no inputs
        : "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14", "fr15"
    );
}

// thanks @FalcoGirgis
inline static void fast_mat_load(const matrix_t* mtx) {
    asm volatile(
        R"(
            fschg
            fmov.d    @%[mtx],xd0
            add        #32,%[mtx]
            pref    @%[mtx]
            add        #-(32-8),%[mtx]
            fmov.d    @%[mtx]+,xd2
            fmov.d    @%[mtx]+,xd4
            fmov.d    @%[mtx]+,xd6
            fmov.d    @%[mtx]+,xd8
            fmov.d    @%[mtx]+,xd10
            fmov.d    @%[mtx]+,xd12
            fmov.d    @%[mtx]+,xd14
            fschg
        )"
        : [mtx] "+r" (mtx)
        :
        :
    );
}
static void hud_draw_target_icon(vec3_t position) {
	vec2i_t screen_size = render_size();
	vec2i_t size = ui_scaled(render_texture_size(target_reticle));
	vec3_t projected;

	float rx = position.x;
	float ry = position.y;
	float rz = position.z;
	mat_load_apply(&vp_mat.cols, &view_mat.cols);
	mat_trans_single3(rx,ry,rz);
	fast_mat_load(&mvp_mat.cols);

	projected.x = rx;
	projected.y = ry;
	projected.z = rz;

	// Not on screen?
	if (
		projected.x < -1 || projected.x > 1 ||
		projected.y < -1 || projected.y > 1 ||
		projected.z >= 1
	) {
		return;
	}

	vec2i_t pos = vec2i(
		(( projected.x + 1.0) / 2.0) * screen_size.x - size.x / 2,
		((-projected.y + 1.0) / 2.0) * screen_size.y - size.y / 2
	);

	render_push_2d(pos, size, rgba(255, 255, 255, 128), target_reticle);
}

void hud_draw(ship_t *ship) {
	// Current lap time
	if (ship->lap >= 0) {
		if (platform_screen_size().y == 360) {
			ui_draw_time(ship->lap_time, ui_scaled_pos(UI_POS_BOTTOM | UI_POS_LEFT, vec2i(16, -25)), UI_SIZE_16, UI_COLOR_DEFAULT);
			for (int i = 0; i < ship->lap && i < NUM_LAPS-1; i++) {
				ui_draw_time(g.lap_times[ship->pilot][i], ui_scaled_pos(UI_POS_BOTTOM | UI_POS_LEFT, vec2i(16, -35 - (10 * i))), UI_SIZE_8, UI_COLOR_ACCENT);
			}
		} else {
			ui_draw_time(ship->lap_time, ui_scaled_pos(UI_POS_BOTTOM | UI_POS_LEFT, vec2i(16, -30)), UI_SIZE_16, UI_COLOR_DEFAULT);
			for (int i = 0; i < ship->lap && i < NUM_LAPS-1; i++) {
				ui_draw_time(g.lap_times[ship->pilot][i], ui_scaled_pos(UI_POS_BOTTOM | UI_POS_LEFT, vec2i(16, -45 - (10 * i))), UI_SIZE_8, UI_COLOR_ACCENT);
			}
		}
	}

	// Current Lap
	int display_lap = max(0, ship->lap + 1);
	ui_draw_text("LAP", ui_scaled(vec2i(15, 8)), UI_SIZE_8, UI_COLOR_ACCENT); 
	ui_draw_number(display_lap, ui_scaled(vec2i(10, 19)), UI_SIZE_16, UI_COLOR_DEFAULT); 
	int width = ui_char_width('0' + display_lap, UI_SIZE_16);
	ui_draw_text("OF", ui_scaled(vec2i((10 + width), 27)), UI_SIZE_8, UI_COLOR_ACCENT);
	ui_draw_number(NUM_LAPS, ui_scaled(vec2i((32 + width), 19)), UI_SIZE_16, UI_COLOR_DEFAULT);

	// Race Position
	if (g.race_type != RACE_TYPE_TIME_TRIAL) {
		ui_draw_text("POSITION", ui_scaled_pos(UI_POS_TOP | UI_POS_RIGHT, vec2i(-90, 8)), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number(ship->position_rank, ui_scaled_pos(UI_POS_TOP | UI_POS_RIGHT, vec2i(-60, 19)), UI_SIZE_16, UI_COLOR_DEFAULT);
	}

	// Framerate
	if (save.show_fps) {
		ui_draw_text("FPS", ui_scaled(vec2i(16, 78)), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number((int)(g.frame_rate), ui_scaled(vec2i(16, 90)), UI_SIZE_8, UI_COLOR_DEFAULT);
	}

#define DRAW_SECTION_NUM 0
	if (DRAW_SECTION_NUM) {
		ui_draw_text("SECTION", ui_scaled(vec2i(16, 100)), UI_SIZE_8, UI_COLOR_ACCENT);
		ui_draw_number(g.ships[g.pilot].section_num, ui_scaled(vec2i(16, 112)), UI_SIZE_8, UI_COLOR_DEFAULT);
	}

	// Lap Record
	ui_draw_text("LAP RECORD", ui_scaled(vec2i(15, 43)), UI_SIZE_8, UI_COLOR_ACCENT);
	ui_draw_time(save.highscores[g.race_class][g.circut][g.highscore_tab].lap_record, ui_scaled(vec2i(15, 55)), UI_SIZE_8, UI_COLOR_DEFAULT);

	// Wrong way
	if (flags_not(ship->flags, SHIP_DIRECTION_FORWARD)) {
		ui_draw_text_centered("WRONG WAY", ui_scaled_pos(UI_POS_MIDDLE | UI_POS_CENTER, vec2i(-20, 0)), UI_SIZE_16, UI_COLOR_ACCENT);
	}

	// Speedo
	int speedo_speed = (g.camera.update_func == camera_update_attract_internal)
		? ship->speed * 7
		: ship->speed;
	hud_draw_speedo(speedo_speed, ship->thrust_mag);

	// Weapon icon
	if (ship->weapon_type != WEAPON_TYPE_NONE) {
		vec2i_t pos;
		vec2i_t size;
		if (platform_screen_size().y == 360) {
			pos = ui_scaled_pos(UI_POS_TOP | UI_POS_CENTER, vec2i(-16, 15));
			size = ui_scaled(vec2i(24, 24));
		}
		else {
			pos = ui_scaled_pos(UI_POS_TOP | UI_POS_CENTER, vec2i(-16, 20));
			size = ui_scaled(vec2i(32, 32));
		}
		uint16_t icon = texture_from_list(weapon_icon_textures, ship->weapon_type-1);
		render_push_2d(pos, size, rgba(255,255,255,255), icon);
	}

	// Lives
	if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		for (int i = 0; i < g.lives; i++) {
			if (platform_screen_size().y == 360) {
				ui_draw_icon(UI_ICON_STAR, ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-26 - 13 * i, -45)), UI_COLOR_DEFAULT);
			} else {
				ui_draw_icon(UI_ICON_STAR, ui_scaled_pos(UI_POS_BOTTOM | UI_POS_RIGHT, vec2i(-26 - 13 * i, -50)), UI_COLOR_DEFAULT);
			}
		}
	}

	// Weapon target reticle
	if (ship->weapon_target) {
		hud_draw_target_icon(ship->weapon_target->position);
	}
}
