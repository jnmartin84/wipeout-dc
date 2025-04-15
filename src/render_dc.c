/**
 * WARNING
 * 
 * You will see many things in here that look "bad" such as:
 * - things being recomputed or re-set that can be computed/set exactly once and behave identically
 * - loops AND manually unrolled sections working on the same arrays for different purposes
 * 
 * You will be tempted to change them for consistency or "efficiency."
 * 
 * When you do, you are going to lose 20% of the rendering throughput.
 * 
 * Something, or many things, are sensitive to the code generation and memory layout after
 * compiling `-O3` with LTO enabled. Cache thrashing over the most MINOR of changes such as:
 * 
 * `int notex = (texture_index == RENDER_NO_TEXTURE);` -- FAST
 * `int notex = !texture_index;` -- 5 to 10 FPS slower
 * 
 * using a constant 255 instead of the calculated a0/1/2/3/4 in the vertex alpha fading code -- 5+ fps drop
 *
 * I have thought of these things, I have tried them. Things here are left as they are for a reason.
 * 
 * The current state of the repo is the version of the code producing the maximum output so far.
 */


#include "system.h"
#include "render.h"
#include "mem.h"
#include "utils.h"
#include "wipeout/game.h"

#include <kos.h>

#include "alloc.h"

pvr_init_params_t pvr_params = {
	{ PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0 },
	1048576,
	0, // 0 is dma disabled
	RENDER_USE_FSAA, // fsaa
	1, // 1 is autosort disabled
	2, // extra OPBs
	0, // Vertex buffer double-buffering enabled
};

void mat_load_apply(const matrix_t* matrix1, const matrix_t* matrix2);
static float shakex = 320.0f;
static float shakey = 240.0f;

#define NEAR_PLANE 16.0f
#define FAR_PLANE (RENDER_FADEOUT_FAR)

#define TEXTURES_MAX 256

#define RENDER_STATEMAP(fg,w,t,c,bl) ((int)(fg) | ((int)(w) << 1) | ((int)(t) << 2) | ((int)(c) << 3) | ((int)(bl) << 4))

extern void memcpy32(const void *dst, const void *src, size_t s);

typedef struct {
	// 0 - 7
	vec2i_t offset;
	// 8 - 15
	vec2i_t size;
} render_texture_t;

extern int load_OP;
extern int drawing_text;
extern int in_race;
extern int in_menu;

uint8_t cur_mode;
float screen_2d_z = -1.0f;

pvr_dr_state_t dr_state;
pvr_vertex_t __attribute__((aligned(32))) vs[5];
void __attribute__((aligned(32))) *ptrs[TEXTURES_MAX] = {0};
uint8_t __attribute__((aligned(32))) last_mode[TEXTURES_MAX] = {0};

uint16_t RENDER_NO_TEXTURE;
const uint16_t HUD_NO_TEXTURE = 65535;

vec2i_t screen_size;

static render_blend_mode_t blend_mode = RENDER_BLEND_NORMAL;

static mat4_t __attribute__((aligned(32))) projection_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) sprite_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) view_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) mvp_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) vp_mat;

static render_texture_t __attribute__((aligned(32))) textures[TEXTURES_MAX];
static uint32_t textures_len = 0;

int dep_en = 0;
int cull_en = 0;
int test_en = 0;
static uint8_t OGNOFILT[TEXTURES_MAX] = {0};
static pvr_poly_hdr_t __attribute__((aligned(32))) *chdr[TEXTURES_MAX] = {0};
pvr_poly_hdr_t chdr_notex;
pvr_poly_hdr_t hud_hdr;

// next power of 2 greater than / equal to v
static inline uint32_t np2(uint32_t v)
{
	if (v < 16) return 16;
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

static void update_header(uint16_t texture_index) {
		uint32_t header1;
		uint32_t header2;
		uint32_t *hp = (uint32_t *)chdr[texture_index];

		header1 = hp[1];
		header2 = hp[2];

		// depth write
		if (dep_en)
			header1 &= ~(1 << 26);
		else
			header1 = (header1 & ~(1 << 26)) | (1 << 26);

		// depth test
		if (test_en)
			header1 = (header1 & 0x1fffffff) | (PVR_DEPTHCMP_GREATER << 29);
		else
			header1 = (header1 & 0x1fffffff) | (PVR_DEPTHCMP_ALWAYS << 29);

		// culling on or off
		if (cull_en)
			header1 = (header1 & 0xEFFFFFFF) | 0x10000000;
		else
			header1 &= 0xEFFFFFFF;

		// clear blending
		header2 &= 0x03FFFFFF;

		// normal or brighter
		if (blend_mode == RENDER_BLEND_NORMAL) {
			header2 |= (PVR_BLEND_SRCALPHA << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_INVSRCALPHA << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (blend_mode == RENDER_BLEND_LIGHTER) {
			header2 |= (PVR_BLEND_SRCALPHA << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ONE << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (blend_mode == RENDER_BLEND_SPECIAL) {
			// srcalpha brighter but bleeds
			header2 |= (PVR_BLEND_DESTCOLOR << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ONE << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (blend_mode == RENDER_BLEND_STUPID) {
			header2 |= (PVR_BLEND_ONE << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ZERO << PVR_TA_PM2_DSTBLEND_SHIFT);
		}

		hp[1] = header1;
		hp[2] = header2;
}

extern int LOAD_UNFILTERED;

void compile_header(uint16_t texture_index) {
	pvr_poly_cxt_t ccxt;
	render_texture_t *t = &textures[texture_index];
	uint32_t filtering = (LOAD_UNFILTERED || !save.filter) ? PVR_FILTER_NONE : PVR_FILTER_BILINEAR;
	OGNOFILT[texture_index] = LOAD_UNFILTERED;
	if ((texture_index != 0)) {
		chdr[texture_index] = memalign(32, sizeof(pvr_poly_hdr_t));

		if (!load_OP) {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);
		} else {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);
		}

		ccxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		ccxt.depth.comparison = PVR_DEPTHCMP_NEVER;
		pvr_poly_compile(chdr[texture_index], &ccxt);
	} else {
		pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
		pvr_poly_compile(&chdr_notex, &ccxt);
	}
}

void render_init(void) {
	pvr_poly_cxt_t ccxt;
	pvr_init(&pvr_params);

	// 3mb VRAM block for glDC allocator
	pvr_ptr_t block = pvr_mem_malloc((1048576*3)+2048);
	if (-1 == alloc_init((void*)block, (1048576*3)))
		exit(-1);

	cull_en = 1;
	dep_en = 1;
	test_en = 1;
	blend_mode = RENDER_BLEND_NORMAL;
	pvr_set_bg_color(0.0f,0.0f,0.0f);

	cur_mode = RENDER_STATEMAP(0,1,1,1,0);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[1].flags = PVR_CMD_VERTEX;
	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[4].flags = PVR_CMD_VERTEX_EOL;

	uint16_t white_pixels[4] = {0xffff,0xffff,0xffff,0xffff};

	RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);

	pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
	ccxt.depth.write = PVR_DEPTHWRITE_DISABLE;
	pvr_poly_compile(&hud_hdr, &ccxt);

	pvr_set_zclip(0.000005f);
}

void render_cleanup(void) {
	; //
}

void render_reset_proj(float farval) {
	float nf = -approx_recip(NEAR_PLANE - farval);
	float f1 = (farval + NEAR_PLANE) * nf;
	float f2 = 2 * farval * NEAR_PLANE * nf;
	projection_mat.m[10] = f1;
	projection_mat.m[14] = f2;

	vp_mat.m[10] = f1;
	vp_mat.m[14] = f2;
}

void render_set_screen_size(vec2i_t size) {
	screen_size = size;

	float aspect = (float)size.x / (float)size.y;
	float fov = (73.75 / 180.0) * 3.14159265358;
	float f = 1.0 / tan(fov / 2);
	float nf = 1.0 / (NEAR_PLANE - 96000.0f);

	projection_mat = mat4(
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (96000.0f + NEAR_PLANE) * nf, -1,
		0, 0, 2 * 96000.0f * NEAR_PLANE * nf, 0);

	memcpy32(&vp_mat.m[0], &projection_mat.m[0], 16*4);
}

vec2i_t render_res_to_size(render_resolution_t res) {
	vec2i_t ssize[2];
	ssize[0].x = 640;
	ssize[0].y = 480;
	
	ssize[1].x = 640;
	ssize[1].y = 360;

	return ssize[(int)res];
}

void render_set_resolution(render_resolution_t res) {
	render_set_screen_size(render_res_to_size(res));
}

void render_set_post_effect(render_post_effect_t post) {
}

vec2i_t render_size(void) {
	return screen_size;
}

void render_frame_prepare(void) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	pvr_scene_begin();

	if (in_menu || in_race)
	 	pvr_list_begin(PVR_LIST_OP_POLY);
	else
		pvr_list_begin(PVR_LIST_TR_POLY);

	pvr_dr_init(&dr_state);

	pvr_set_zclip(0.000005f);
}

void render_frame_end(void) {
    pvr_list_finish();
    pvr_scene_finish();

	screen_2d_z = -1.0f;
}

void render_set_view(vec3_t pos, vec3_t angles) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	view_mat = mat4_identity();

	mat4_set_translation(&view_mat, vec3(0, 0, 0));
	mat4_set_roll_pitch_yaw(&view_mat, vec3(angles.x, -angles.y + F_PI, angles.z + F_PI));
	mat4_translate(&view_mat, vec3_inv(pos));
	mat4_set_yaw_pitch_roll(&sprite_mat, vec3(-angles.x, angles.y - F_PI, 0));
}

void render_set_view_2d(void) {
	render_set_depth_test(false);
	render_set_depth_write(false);

	float near = -1;
	float far = 1;
	float left = 0;
	float right = screen_size.x;
	float bottom = screen_size.y;
	float top = 0;
	float lr = 1 / (left - right);
	float bt = 1 / (bottom - top);
	float nf = 1 / (near - far);

	mvp_mat = mat4(
		-2 * lr, 0, 0, 0,
		0, -2 * bt, 0, 0,
		0, 0, 2 * nf, 0,
		(left + right) * lr, (top + bottom) * bt, (far + near) * nf, 1);

	mat_load(&mvp_mat.cols);
}

void render_set_model_mat(mat4_t *m) {
	mat_load_apply(&vp_mat.cols, &view_mat.cols);
	mat_apply(&m->cols);
}

void render_set_model_ident(void) {
	mat_load_apply(&vp_mat.cols, &view_mat.cols);
}

void render_set_depth_write(bool enabled) {
	if ((int)enabled != dep_en) {
		dep_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);
	}
}

void render_set_depth_test(bool enabled) {
	if ((int)enabled != test_en) {
		test_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);
	}
}

void render_set_depth_offset(float offset) {
	; //
}

void render_set_screen_position(vec2_t pos) {
	; //
}

void render_set_blend_mode(render_blend_mode_t new_mode) {
	if (new_mode != blend_mode) {
		blend_mode = new_mode;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);
	}
}

void render_set_cull_backface(bool enabled) {
	if (enabled != cull_en) {
		cull_en = enabled;
		cur_mode = RENDER_STATEMAP(0,dep_en,test_en,cull_en,blend_mode);
	}
}

static uint16_t last_index = 256;

static float wout;

#define cliplerp(__a, __b, __t) ((__a) + (((__b) - (__a))*(__t)))

static uint32_t color_lerp(float ft, uint32_t c1, uint32_t c2) {
	if (ft < 0.5f) return c1;
	return c2;	
}

static void nearz_clip(pvr_vertex_t *v0, pvr_vertex_t *v1, pvr_vertex_t *outv, float w0, float w1) {
	const float d0 = w0 + v0->z;
	const float d1 = w1 + v1->z;
	float d1subd0 = d1 - d0;
	if (d1subd0 == 0.0f) d1subd0 = 0.0001f;
	float t = (fabsf(d0) * approx_recip(d1subd0)) + 0.000001f;
	outv->x = cliplerp(v0->x, v1->x, t);
	outv->y = cliplerp(v0->y, v1->y, t);
	outv->z = cliplerp(v0->z, v1->z, t);
	outv->u = cliplerp(v0->u, v1->u, t);
	outv->v = cliplerp(v0->v, v1->v, t);
	outv->argb = color_lerp(t, v0->argb, v1->argb);
	outv->oargb = color_lerp(t, v0->oargb, v1->oargb);
	wout = cliplerp(w0, w1, t);
}

static float shakeCount = 0;
void SetShake(float duration) {
	shakeCount = (duration / 30.0f);
}

void ShakeScreen(void) {
	if(shakeCount > 0.0f) {
		shakex = 320.0f + ((-(rand_float(0.0f, shakeCount)) + (shakeCount * 0.5f))*45.0f);
		shakey = 240.0f + ((-(rand_float(0.0f, shakeCount)) + (shakeCount * 0.5f))*45.0f);
		shakeCount -= system_tick();
	}
	else {
		shakex = 320.0f; 
		shakey = 240.0f;
		shakeCount = 0.0f;
	}
}       

static inline void perspdiv(pvr_vertex_t *v, float w) {
	const float invw = approx_recip(w);
	float x = v->x * invw;
	float y = v->y * invw;

#if RENDER_USE_FSAA
	x = 640.0f + (640.0f * x);
#else
	x = shakex + (320.0f * x);
#endif
	// 4:3 or 16:9 anamorphic
	y = shakey - (240.0f * y);

	v->x = x;
	v->y = y;
	v->z = invw;
}

void render_hud_quad(uint16_t texture_index) {
	float w0,w1,w2,w3;
	screen_2d_z += 0.0005f;

	vs[0].z = screen_2d_z;
	vs[1].z = screen_2d_z;
	vs[2].z = screen_2d_z;
	vs[3].z = screen_2d_z;

	pvr_prim(&hud_hdr, sizeof(pvr_poly_hdr_t));

	mat_trans_single3_nodivw(vs[0].x, vs[0].y, vs[0].z, w0);
	mat_trans_single3_nodivw(vs[1].x, vs[1].y, vs[1].z, w1);
	mat_trans_single3_nodivw(vs[2].x, vs[2].y, vs[2].z, w2);
	mat_trans_single3_nodivw(vs[3].x, vs[3].y, vs[3].z, w3);

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);
	perspdiv(&vs[3], w3);

	pvr_prim(&vs[0], sizeof(pvr_vertex_t) * 4);
}

void render_quad(uint16_t texture_index) {
	float w0,w1,w2,w3,w4;
	uint8_t cl0, cl1, cl2, cl3;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	mat_trans_single3_nodivw(vs[0].x, vs[0].y, vs[0].z, w0);
	mat_trans_single3_nodivw(vs[1].x, vs[1].y, vs[1].z, w1);
	mat_trans_single3_nodivw(vs[2].x, vs[2].y, vs[2].z, w2);
	mat_trans_single3_nodivw(vs[3].x, vs[3].y, vs[3].z, w3);

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].z < -w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].z < -w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) | !(vs[2].z < -w2);
    cl2 = (cl2 << 1) | !(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

    cl3 = !(vs[3].z >  w3);
    cl3 = (cl3 << 1) | !(vs[3].z < -w3);
    cl3 = (cl3 << 1) | !(vs[3].y >  w3);
    cl3 = (cl3 << 1) | !(vs[3].y < -w3);
    cl3 = (cl3 << 1) | !(vs[3].x >  w3);
    cl3 = (cl3 << 1) | !(vs[3].x < -w3);

    if ((cl0 | cl1 | cl2 | cl3) != 0x3f)
		return;

	uint32_t vismask = (((vs[0].z >= -w0)) | (((vs[1].z >= -w1)) << 1) | (((vs[2].z >= -w2)) << 2) | (((vs[3].z >= -w3)) << 3));
	//vs[2].flags = PVR_CMD_VERTEX;
	int sendverts = 4;

	if (vismask == 15) {
		perspdiv(&vs[3], w3);
		goto quad_sendit;
	} else {
		switch (vismask)
		{
		// quad only 0 visible
		case 1:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad only 1 visible
		case 2:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			nearz_clip(&vs[1], &vs[3], &vs[2], w1, w3);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 1 visible
		case 3:
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad only 2 visible
		case 4:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[1], w2, w3);
			w1 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 2 visible
		case 5:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[3], w2, w3);
			w3 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 1 + 2 visible
		case 7:
			sendverts = 5;

			nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
			w4 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad only 3 visible
		case 8:
			sendverts = 3;

			nearz_clip(&vs[1], &vs[3], &vs[0], w1, w3);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[2], w2, w3);
			w2 = wout;

			memcpy32(&vs[1], &vs[3], 32);
			w1 = w3;

			vs[1].flags = PVR_CMD_VERTEX;
			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 1 + 3 visible
		case 10:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[2], w2, w3);
			w2 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 1 + 3 visible
		case 11:
			sendverts = 5;

			nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
			w4 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad 2 + 3 visible
		case 12:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[1], w1, w3);
			w1 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 2 + 3 visible
		case 13:
			sendverts = 5;

			memcpy32(&vs[4], &vs[3], 32);
			w4 = w3;

			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad 1 + 2 + 3 visible
		case 14:
			sendverts = 5;

			memcpy32(&vs[4], &vs[2], 32);
			w4 = w2;

			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;
		}
	}

quad_sendit:
	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

	// anything but boost/item pads
	if (vs[0].oargb < 3) {
		// both of these need to be checked at top level, not one with one nested
		if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
			last_index = texture_index;

			last_mode[texture_index] = cur_mode;

			update_header(texture_index);
	
			if(__builtin_expect(notex,0))
				pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
			else
				pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}

		pvr_prim(vs, sendverts * 32);
	} else {
		// boost/item pads use blending to be bright enough/glow
		render_blend_mode_t prev_blend = blend_mode;
		int prev_cull = cull_en;
		last_index = texture_index;

		render_set_cull_backface(true);

		// base quad
		render_set_blend_mode(RENDER_BLEND_STUPID);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, sendverts * 32);

		// blended quad
		render_set_blend_mode(RENDER_BLEND_SPECIAL);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		// slight offset up and toward near plane
		for (int i=0;i<sendverts;i++) {
			vs[i].y += 0.00005f;
			vs[i].z += 0.00005f;
		}

		pvr_prim(vs, sendverts * 32);

		// restore blend mode that was set before any of this
		render_set_blend_mode(prev_blend);

		render_set_cull_backface(prev_cull);
	}
}

extern int shields_active;

void render_tri(uint16_t texture_index) {
	float w0,w1,w2,w3;
	uint8_t cl0, cl1, cl2;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	mat_trans_single3_nodivw(vs[0].x, vs[0].y, vs[0].z, w0);
	mat_trans_single3_nodivw(vs[1].x, vs[1].y, vs[1].z, w1);
	mat_trans_single3_nodivw(vs[2].x, vs[2].y, vs[2].z, w2);

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].z < -w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].z < -w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) | !(vs[2].z < -w2);
    cl2 = (cl2 << 1) | !(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

	if ((cl0 | cl1 | cl2) != 0x3f)
		return;

	uint32_t vismask = (((vs[0].z >= -w0)) | (((vs[1].z >= -w1)) << 1) | (((vs[2].z >= -w2)) << 2));
	int sendverts = 3;
//	vs[2].flags = PVR_CMD_VERTEX_EOL;

	if (vismask == 7) {
		goto tri_sendit;
	} else {
		switch (vismask)
		{
		case 1:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			break;
		case 2:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[2], w1, w2);
			w2 = wout;

			break;
		case 3:
			sendverts = 4;

			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		case 4:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[1], w1, w2);
			w1 = wout;

			break;
		case 5:
			sendverts = 4;

			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		case 6:
			sendverts = 4;

			memcpy32(&vs[3], &vs[2], 32);
			w3 = w2;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		}
	}

tri_sendit:
	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

	// don't do anything header-related if we're on the same texture or render mode as the last call
	if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
		last_index = texture_index;

		last_mode[texture_index] = cur_mode;

		update_header(texture_index);
	
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
	}

	pvr_prim(vs, sendverts * 32);
}

void render_quad_noxform(uint16_t texture_index, float *w) {
	float w0,w1,w2,w3,w4;
  	uint8_t cl0, cl1, cl2, cl3;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	w0 = w[0];
	w1 = w[1];
	w2 = w[2];
	w3 = w[3];

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].z < -w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].z < -w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) | !(vs[2].z < -w2);
    cl2 = (cl2 << 1) | !(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

    cl3 = !(vs[3].z >  w3);
    cl3 = (cl3 << 1) | !(vs[3].z < -w3);
    cl3 = (cl3 << 1) | !(vs[3].y >  w3);
    cl3 = (cl3 << 1) | !(vs[3].y < -w3);
    cl3 = (cl3 << 1) | !(vs[3].x >  w3);
    cl3 = (cl3 << 1) | !(vs[3].x < -w3);

    if ((cl0 | cl1 | cl2 | cl3) != 0x3f)
		return;

	uint32_t vismask = (((vs[0].z >= -w0)) | (((vs[1].z >= -w1)) << 1) | (((vs[2].z >= -w2)) << 2) | (((vs[3].z >= -w3)) << 3));

//	vs[2].flags = PVR_CMD_VERTEX;
//	vs[3].flags = PVR_CMD_VERTEX_EOL;

	int sendverts = 4;

	if (vismask == 15) {
		perspdiv(&vs[3], w3);
		goto quad_sendit;
	} else {
		switch (vismask)
		{
		// quad only 0 visible
		case 1:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad only 1 visible
		case 2:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			nearz_clip(&vs[1], &vs[3], &vs[2], w1, w3);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 1 visible
		case 3:
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad only 2 visible
		case 4:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[1], w2, w3);
			w1 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 0 + 2 visible
		case 5:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[3], w2, w3);
			w3 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 1 + 2 visible
		case 7:
			sendverts = 5;

			nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
			w4 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad only 3 visible
		case 8:
			sendverts = 3;

			nearz_clip(&vs[1], &vs[3], &vs[0], w1, w3);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[2], w2, w3);
			w2 = wout;

			memcpy32(&vs[1], &vs[3], 32);
			w1 = w3;

			vs[1].flags = PVR_CMD_VERTEX;
			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad 1 + 3 visible
		case 10:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[2], &vs[3], &vs[2], w2, w3);
			w2 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 1 + 3 visible
		case 11:
			sendverts = 5;

			nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
			w4 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad 2 + 3 visible
		case 12:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[3], &vs[1], w1, w3);
			w1 = wout;

			perspdiv(&vs[3], w3);

			break;

		// quad 0 + 2 + 3 visible
		case 13:
			sendverts = 5;

			memcpy32(&vs[4], &vs[3], 32);
			w4 = w3;

			nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
			w3 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;

		// quad 1 + 2 + 3 visible
		case 14:
			sendverts = 5;

			memcpy32(&vs[4], &vs[2], 32);
			w4 = w2;

			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;

			vs[3].flags = PVR_CMD_VERTEX;
			vs[4].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);
			perspdiv(&vs[4], w4);

			break;
		}
	}

quad_sendit:
	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

	if (vs[0].oargb < 3) {
		if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
			last_index = texture_index;

			last_mode[texture_index] = cur_mode;

			update_header(texture_index);
	
			if(__builtin_expect(notex,0))
				pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
			else
				pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}

		pvr_prim(vs, sendverts * 32);
	} else {
		int prev_cull = cull_en;
		render_blend_mode_t prev_blend = blend_mode;
		last_index = texture_index;

		render_set_cull_backface(true);

		render_set_blend_mode(RENDER_BLEND_STUPID);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, sendverts * 32);

		render_set_blend_mode(RENDER_BLEND_SPECIAL);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		for (int i=0;i<sendverts;i++) {
			vs[i].y += 0.00005f;
			vs[i].z += 0.00005f;
		}

		pvr_prim(vs, sendverts * 32);

		render_set_blend_mode(prev_blend);

		render_set_cull_backface(prev_cull);
	}
}

void render_tri_noxform(uint16_t texture_index, float *w) {
	float w0,w1,w2,w3;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	w0 = w[0];
	w1 = w[1];
	w2 = w[2];

  	uint8_t cl0, cl1, cl2;

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].z < -w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].z < -w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) | !(vs[2].z < -w2);
    cl2 = (cl2 << 1) | !(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

	if ((cl0 | cl1 | cl2) != 0x3f)
		return;
 
	uint32_t vismask = (((vs[0].z >= -w0)) | (((vs[1].z >= -w1)) << 1) | (((vs[2].z >= -w2)) << 2));
	int sendverts = 3;
//	vs[2].flags = PVR_CMD_VERTEX_EOL;

	if (vismask == 7) {
		goto tri_sendit;
	} else {
		switch (vismask)
		{
		case 1:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			break;
		case 2:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[2], w1, w2);
			w2 = wout;

			break;
		case 3:
			sendverts = 4;

			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		case 4:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[1], w1, w2);
			w1 = wout;

			break;
		case 5:
			sendverts = 4;

			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		case 6:
			sendverts = 4;

			memcpy32(&vs[3], &vs[2], 32);
			w3 = w2;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[3].flags = PVR_CMD_VERTEX_EOL;
			perspdiv(&vs[3], w3);

			break;
		}
	}

tri_sendit:
	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

	// don't do anything header-related if we're on the same texture or render mode as the last call
	render_set_blend_mode(RENDER_BLEND_NORMAL);
	if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
		last_index = texture_index;

		last_mode[texture_index] = cur_mode;

		update_header(texture_index);
	
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
	}

	pvr_prim(vs, sendverts * 32);
}

void render_quad_noxform_noclip(uint16_t texture_index, float *w) {
	float w0,w1,w2,w3;
  	uint8_t cl0, cl1, cl2, cl3;

	int notex = (texture_index == RENDER_NO_TEXTURE);

	w0 = w[0];
	w1 = w[1];
	w2 = w[2];
	w3 = w[3];

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) |!(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

    cl3 = !(vs[3].z >  w3);
    cl3 = (cl3 << 1) | !(vs[3].y >  w3);
    cl3 = (cl3 << 1) | !(vs[3].y < -w3);
    cl3 = (cl3 << 1) | !(vs[3].x >  w3);
    cl3 = (cl3 << 1) | !(vs[3].x < -w3);

    if ((cl0 | cl1 | cl2 | cl3) != 0x1f)
		return;
 
//	vs[2].flags = PVR_CMD_VERTEX;
//	vs[3].flags = PVR_CMD_VERTEX_EOL;

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);
	perspdiv(&vs[3], w3);

	if (vs[0].oargb < 3) {
		if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
			last_index = texture_index;

			last_mode[texture_index] = cur_mode;

			update_header(texture_index);
	
			if(__builtin_expect(notex,0))
				pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
			else
				pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}

		pvr_prim(vs, 128);
	} else {
		int prev_cull = cull_en;
		render_blend_mode_t prev_blend = blend_mode;
		last_index = texture_index;

		render_set_cull_backface(true);

		render_set_blend_mode(RENDER_BLEND_STUPID);
		last_mode[texture_index] = cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, 128);

		render_set_blend_mode(RENDER_BLEND_SPECIAL);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		vs[0].y += 0.00005f;
		vs[0].z += 0.00005f;
		vs[1].y += 0.00005f;
		vs[1].z += 0.00005f;
		vs[2].y += 0.00005f;
		vs[2].z += 0.00005f;
		vs[3].y += 0.00005f;
		vs[3].z += 0.00005f;
		pvr_prim(vs, 128);

		render_set_blend_mode(prev_blend);

		render_set_cull_backface(prev_cull);
	}
}


void render_tri_noxform_noclip(uint16_t texture_index, float *w) {
	float w0,w1,w2;
 	uint8_t cl0, cl1, cl2;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	w0 = w[0];
	w1 = w[1];
	w2 = w[2];

    cl0 = !(vs[0].z >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y >  w0);
    cl0 = (cl0 << 1) | !(vs[0].y < -w0);
    cl0 = (cl0 << 1) | !(vs[0].x >  w0);
    cl0 = (cl0 << 1) | !(vs[0].x < -w0);

    cl1 = !(vs[1].z >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y >  w1);
    cl1 = (cl1 << 1) | !(vs[1].y < -w1);
    cl1 = (cl1 << 1) | !(vs[1].x >  w1);
    cl1 = (cl1 << 1) | !(vs[1].x < -w1);

    cl2 = !(vs[2].z >  w2);
    cl2 = (cl2 << 1) |!(vs[2].y >  w2);
    cl2 = (cl2 << 1) | !(vs[2].y < -w2);
    cl2 = (cl2 << 1) | !(vs[2].x >  w2);
    cl2 = (cl2 << 1) | !(vs[2].x < -w2);

    if ((cl0 | cl1 | cl2) != 0x1f)
		return;

//	vs[2].flags = PVR_CMD_VERTEX_EOL;

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

	// don't do anything header-related if we're on the same texture or render mode as the last call
	//render_set_blend_mode(RENDER_BLEND_NORMAL);

	if (last_index != texture_index || cur_mode != last_mode[texture_index]) {
		last_index = texture_index;

		last_mode[texture_index] = cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
	}

	pvr_prim(vs, 96);
}

void render_push_sprite(vec3_t pos, vec2i_t size, uint32_t lcol, uint16_t texture_index) {
	screen_2d_z += 0.0005f;
	// this ordering fixes the drawing of sprites without disabling culling
	vec3_t p1 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5f, -size.y * 0.5f, screen_2d_z), &sprite_mat));
	vec3_t p2 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5f, -size.y * 0.5f, screen_2d_z), &sprite_mat));
	vec3_t p3 = vec3_add(pos, vec3_transform(vec3( size.x * 0.5f,  size.y * 0.5f, screen_2d_z), &sprite_mat));
	vec3_t p4 = vec3_add(pos, vec3_transform(vec3(-size.x * 0.5f,  size.y * 0.5f, screen_2d_z), &sprite_mat));
	render_texture_t *t = &textures[texture_index];
	float rpw = approx_recip(t->offset.x);
	float rph = approx_recip(t->offset.y);
//	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = p1.x;
	vs[0].y = p1.y;
	vs[0].z = p1.z;
	vs[0].u = rpw;
	vs[0].v = rph;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

//	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = p2.x;
	vs[1].y = p2.y;
	vs[1].z = p2.z;
	vs[1].u = (t->size.x - 1.0f) * rpw;
	vs[1].v = rph;
	vs[1].argb = lcol;
//	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;
	vs[2].x = p3.x;
	vs[2].y = p3.y;
	vs[2].z = p3.z;
	vs[2].u = 1.0f * rpw;
	vs[2].v = (t->size.y - 1.0f) * rph;
	vs[2].argb = lcol;
//	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = p4.x;
	vs[3].y = p4.y;
	vs[3].z = p4.z;
	vs[3].u = (t->size.x - 1.0f) * rpw;
	vs[3].v = (t->size.y - 1.0f) * rph;
	vs[3].argb = lcol;
//	vs[3].oargb = 0;

	render_quad(texture_index);
}

void render_push_2d(vec2i_t pos, vec2i_t size, rgba_t color, uint16_t texture_index) {
	render_push_2d_tile(pos, vec2i(0, 0), render_texture_size(texture_index), size, color, texture_index);
}

void render_push_2d_tile(vec2i_t pos, vec2i_t uv_offset, vec2i_t uv_size, vec2i_t size, rgba_t color, uint16_t texture_index) {
//	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	vec2i_t tsize = render_texture_padsize(texture_index);
	float rpw = approx_recip((float)tsize.x);
	float rph = approx_recip((float)tsize.y);

	screen_2d_z += 0.0005f;

	uint32_t lcol = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b);

//	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = pos.x;
	vs[0].y = pos.y;
	vs[0].z = screen_2d_z;
	vs[0].u = uv_offset.x * rpw;
	vs[0].v = uv_offset.y * rph;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

//	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = pos.x + size.x;
	vs[1].y = pos.y;
	vs[1].z = screen_2d_z;
	vs[1].u = (uv_offset.x + uv_size.x) * rpw;
	vs[1].v = uv_offset.y * rph;
	vs[1].argb = lcol;
//	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;
	vs[2].x = pos.x;
	vs[2].y = pos.y + size.y;
	vs[2].z = screen_2d_z;
	vs[2].u = uv_offset.x * rpw;
	vs[2].v = (uv_offset.y + uv_size.y) * rph;
	vs[2].argb = lcol;
//	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = pos.x + size.x;
	vs[3].y = pos.y + size.y;
	vs[3].z = screen_2d_z;
	vs[3].u = (uv_offset.x + uv_size.x) * rpw;
	vs[3].v = (uv_offset.y + uv_size.y) * rph;
	vs[3].argb = lcol;
//	vs[3].oargb = 0;

	render_quad(texture_index);
}

#include "platform.h"
uint16_t render_texture_create(uint32_t tw, uint32_t th, uint16_t *pixels) {
	uint16_t texture_index = textures_len++;

	if (texture_index != 0) {
		int wp2 = np2(tw);
		int hp2 = np2(th);

		if (g.race_class == RACE_CLASS_VENOM && g.circut == CIRCUT_TERRAMAX && texture_index == 0x88) {
			uint32_t dcpad_size;
			ptrs[texture_index] = alloc_malloc(NULL, wp2 * hp2 * 2);
			// if we ran out of VRAM (we don't, but if we did), handle it by just using color poly header
			if (ptrs[texture_index == 0])
				goto createbail;

			textures[texture_index] = (render_texture_t){ {wp2, hp2}, {tw, th} };

			void *objdata = platform_load_asset("wipeout/common/sonic.raw", &dcpad_size);
			pvr_txr_load(objdata, ptrs[texture_index], 128*64*2);
			mem_temp_free(objdata);
		} else {
			uint16_t *tmpstore = (uint16_t *)mem_temp_alloc(sizeof(uint16_t)*wp2*hp2);

			// there's something weird with textures of the bottom of the right wing of every ship
			// and the ring through the top of the stopwatch in the options menu
			// i dont really know what is going on there, but this is a color that doesnt look ridiculous
			// for any of those contexts
			for (int i = 0; i < wp2 * hp2; i++)
				tmpstore[i] = 0xBDEF;

			ptrs[texture_index] = alloc_malloc(NULL, wp2 * hp2 * 2);
			// if we ran out of VRAM (we don't, but if we did), handle it by just using color poly header
			if (ptrs[texture_index == 0])
				goto createbail;

			textures[texture_index] = (render_texture_t){ {wp2, hp2}, {tw, th} };

			for (uint32_t y = 0; y < th; y++)
				for(uint32_t x = 0;x < tw; x++)
					tmpstore[(y*wp2) + x] = pixels[(y*tw) + x];

			pvr_txr_load_ex(tmpstore, ptrs[texture_index], wp2, hp2, PVR_TXRLOAD_16BPP);
			mem_temp_free(tmpstore);
		}
	}

	compile_header(texture_index);

createbail:
	if (ptrs[texture_index] == 0)
		chdr[texture_index] = &chdr_notex;

	return texture_index;
}

vec2i_t render_texture_size(uint16_t texture_index) {
	return textures[texture_index].size;
}

vec2i_t render_texture_padsize(uint16_t texture_index) {
	return textures[texture_index].offset;
}

void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels) {
	; // only used by pl_mpeg for intro video
}

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	for (uint16_t curlen = len;curlen<textures_len;curlen++) {
		if (ptrs[curlen]) {
			alloc_free(NULL, ptrs[curlen]);
			ptrs[curlen] = 0;

			free(chdr[curlen]);
			chdr[curlen] = NULL;
		}

		last_mode[curlen] = 0;
		OGNOFILT[curlen] = 0;
	}

	textures_len = len;

	for (int i=1;i<len;i++) {
		if (!OGNOFILT[i]) {
			uint32_t *hp = (uint32_t *)chdr[i];
			uint32_t header2;
			header2 = hp[2];
			header2 = (header2 & ~PVR_TA_PM2_FILTER_MASK) | ((save.filter * 2) << PVR_TA_PM2_FILTER_SHIFT);
			hp[2] = header2;
		}
	}

	// Clear completely and recreate the default white texture
	if (len == 0) {
		uint16_t white_pixels[4] = {0xffff, 0xffff, 0xffff, 0xffff};
		RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);
		return;
	}
}

void render_textures_dump(const char *path) {
	; //
}