#ifndef RENDER_H
#define RENDER_H

#include "types.h"

typedef enum {
	RENDER_BLEND_NORMAL,
	RENDER_BLEND_LIGHTER,
	RENDER_BLEND_SPECIAL,
	RENDER_BLEND_STUPID,
	RENDER_BLEND_SPECIAL2,
	RENDER_BLEND_BRIGHTBRIGHT,
} render_blend_mode_t;

typedef enum {
	RENDER_RES_43,
	RENDER_RES_169A,
	RENDER_RES_169L,
} render_resolution_t;

typedef enum {
	RENDER_POST_NONE,
	RENDER_POST_CRT,
	NUM_RENDER_POST_EFFCTS,
} render_post_effect_t;

typedef struct {
	// 0
	int last_index;
	// 4
	int cur_mode;
	// 8
	render_blend_mode_t blend_mode;
	// 12
	uint8_t cull_dirty;
	// 13
	uint8_t cull_en;
	// 14
	uint8_t dep_en;
	// 15
	uint8_t test_en;
	// 16
	uint8_t load_OP;
	// 17
	uint8_t LOAD_UNFILTERED;
	// 18
	uint8_t in_menu;
	// 19
	uint8_t in_race;
} global_render_state_t;
extern global_render_state_t render_state;

// dont do it, you'll see lol
#define RENDER_USE_FSAA 0

#define RENDER_USE_MIPMAPS 1

#define RENDER_FADEOUT_NEAR 48000.0f
#define RENDER_FADEOUT_FAR 64000.0f

extern uint16_t RENDER_NO_TEXTURE;

void render_init();
void render_cleanup(void);
vec2i_t render_res_to_size(render_resolution_t res);
void render_reset_proj(float farval);
void render_set_screen_size(vec2i_t size);
void render_set_resolution(render_resolution_t res);
void render_set_post_effect(render_post_effect_t post);
vec2i_t render_size(void);

void render_frame_prepare(void);
void render_frame_end(void);

void render_set_view(vec3_t pos, vec3_t angles);
void render_set_view_2d(void);
void render_set_model_mat(mat4_t *m);
void render_set_model_ident(void);
void render_set_depth_write(bool enabled);
void render_set_depth_test(bool enabled);
void render_set_depth_offset(float offset);
void render_set_screen_position(vec2_t pos);
void render_set_blend_mode(render_blend_mode_t mode);
void render_set_cull_backface(bool enabled);

void render_hud_quad(uint16_t texture);

void __attribute__((noinline)) render_quad(uint16_t texture);
void __attribute__((noinline)) render_tri(uint16_t texture);
void __attribute__((noinline)) render_quad_noxform(uint16_t texture_index, float *w);
void __attribute__((noinline)) render_tri_noxform(uint16_t texture_index, float *w);
void __attribute__((noinline)) render_quad_noxform_noclip(uint16_t texture_index, float *w);
void __attribute__((noinline)) render_tri_noxform_noclip(uint16_t texture_index, float *w);

void  __attribute__((noinline)) render_push_sprite(vec3_t pos, vec2i_t size, uint32_t color, uint16_t texture);
void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture);
void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index);

uint16_t render_texture_create(uint32_t width, uint32_t height, uint16_t *pixels);
vec2i_t render_texture_size(uint16_t texture_index);
vec2i_t render_texture_padsize(uint16_t texture_index);
void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels);
uint16_t render_textures_len(void);
void render_textures_reset(uint16_t len);
void render_textures_dump(const char *path);

#endif
