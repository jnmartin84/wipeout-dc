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

void mat4_mul_fipr(mat4_t *res, mat4_t *a, mat4_t *b);

extern float RENDERDIST;

#define NEAR_PLANE 16.0f
#define FAR_PLANE (RENDER_FADEOUT_FAR)
#define FARPLANESCALE (RENDERDIST * 0.01569f)

#define TEXTURES_MAX 1024

#define RENDER_STATEMAP(fg,w,t,c,bl) ((int)(fg) | ((int)(w) << 1) | ((int)(t) << 2) | ((int)(c) << 3) | ((int)(bl) << 4))

extern void memcpy32(const void *dst, const void *src, size_t s);

typedef struct {
	// 0 - 7
	vec2i_t offset;
	// 8 - 15
	vec2i_t size;
} render_texture_t;

extern int no_fade;
extern int load_OP;
extern int drawing_text;

uint8_t cur_mode;
int modes_dirty = 0;
float screen_2d_z = -1.0f;

pvr_dr_state_t dr_state;
pvr_vertex_t __attribute__((aligned(32))) vs[5];
pvr_ptr_t __attribute__((aligned(32))) ptrs[TEXTURES_MAX] = {0};
uint8_t __attribute__((aligned(32))) last_mode[TEXTURES_MAX] = {0};

uint16_t RENDER_NO_TEXTURE;
const uint16_t HUD_NO_TEXTURE = 65535;

static vec2i_t screen_size;
static vec2i_t backbuffer_size;

static render_blend_mode_t blend_mode = RENDER_BLEND_NORMAL;

static mat4_t __attribute__((aligned(32))) projection_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) sprite_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) view_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) mvp_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) vp_mat;

static render_texture_t __attribute__((aligned(32))) textures[TEXTURES_MAX];
static uint32_t textures_len = 0;

static render_resolution_t render_res;

int dep_en = 0;
int cull_en = 0;
int test_en = 0;

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
extern uint8_t bs;
extern uint8_t bd;

static void update_header(uint16_t texture_index, uint32_t notflat) {
		uint32_t header1;
		uint32_t header2;
		uint32_t *hp = (uint32_t *)chdr[texture_index];

		header1 = hp[1];
		header2 = hp[2];

		if ((!save.shading) && texture_index) {
			uint32_t header0 = hp[0];
// if you want to get flat-shaded polys for F3,F4,FT3,FT4, uncomment next 4 lines, but FPS will drop
			if (notflat < 2u)
				header0 = (header0 & 0xfffffff0) | (0x8 + (notflat << 1));
			else
				header0 = (header0 & 0xfffffff0) | 0xa;
//		else
//			hp[0] = 0x8284000a;
			hp[0] = header0;
		}

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

	if ((texture_index != 0)) {
		chdr[texture_index] = memalign(32, sizeof(pvr_poly_hdr_t));

		if (!load_OP) {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);			
		} else {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);
		}

		ccxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		//ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		ccxt.depth.comparison = PVR_DEPTHCMP_NEVER;
		//ccxt.txr.uv_flip = PVR_UVFLIP_U;
		pvr_poly_compile(chdr[texture_index], &ccxt);
	} else {
		pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
		//ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		pvr_poly_compile(&chdr_notex, &ccxt);
	}
}


void render_init(vec2i_t screen_size) {	
	pvr_poly_cxt_t ccxt;

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
	ccxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
	pvr_poly_compile(&hud_hdr, &ccxt);

	render_res = RENDER_RES_NATIVE;
	render_set_screen_size(screen_size);

	pvr_set_zclip(0.000010f);
}

void render_cleanup(void) {

}


void render_set_screen_size(vec2i_t size) {
	screen_size = size;

	float aspect = (float)size.x / (float)size.y;
	float fov = (73.75 / 180.0) * 3.14159265358;
	float f = 1.0 / tan(fov / 2);
	float nf = 1.0 / (NEAR_PLANE - RENDERDIST);
	projection_mat = mat4(
		f / aspect, 0, 0, 0,
		0, f, 0, 0,
		0, 0, (RENDERDIST + NEAR_PLANE) * nf, -1,
		0, 0, 2 * RENDERDIST * NEAR_PLANE * nf, 0);

	memcpy32(&vp_mat.m[0], &projection_mat.m[0], 16*4);
}

void render_set_resolution(render_resolution_t res) {
	render_res = res;

	if (res == RENDER_RES_NATIVE) {
		backbuffer_size = screen_size;
	}
	else {
		float aspect = (float)screen_size.x / (float)screen_size.y;
		if (res == RENDER_RES_240P) {
			backbuffer_size = vec2i(240.0 * aspect, 240);
		} else if (res == RENDER_RES_480P) {
			backbuffer_size = vec2i(480.0 * aspect, 480);	
		} else {
			die("Invalid resolution: %d", res);
		}
	}
}

void render_set_post_effect(render_post_effect_t post) {
}

vec2i_t render_size(void) {
	return backbuffer_size;
}

extern int in_race;
extern int in_menu;

void render_frame_prepare(void) {
	render_set_depth_write(true);
	render_set_depth_test(true);

	pvr_scene_begin();

	if (in_menu || in_race)
	 	pvr_list_begin(PVR_LIST_OP_POLY);
	else
		pvr_list_begin(PVR_LIST_TR_POLY);

	pvr_dr_init(&dr_state);

	// 3+ fps faster with these enabled
	vs[0].flags = PVR_CMD_VERTEX;
	vs[1].flags = PVR_CMD_VERTEX;
	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[4].flags = PVR_CMD_VERTEX_EOL;
}


void render_frame_end(void) {
	pvr_list_finish();
	pvr_scene_finish();
	pvr_wait_ready();

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

static mat4_t __attribute__((aligned(32))) vm_mat;
static mat4_t __attribute__((aligned(32))) vm2_mat;

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
	memcpy32(&vm2_mat.m[0], &mvp_mat.m[0], 16*4);
	mat_load(&vm2_mat.cols);
}

void render_set_model_mat(mat4_t *m) {
	mat4_mul_fipr(&vm_mat, &view_mat, m);
	mat4_mul(&mvp_mat, &vp_mat, &vm_mat);
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
	// ?
}

void render_set_screen_position(vec2_t pos) {
	//
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

static uint16_t last_index = 1025;

vec3_t render_transform(vec3_t pos) {
	//return vec3_transform(vec3_transform(pos, &view_mat), &projection_mat);
	return vec3_transform(pos, &mvp_mat);
}

static float wout; //xout,yout,zout,,uout,vout;

#define cliplerp(__a, __b, __t) ((__a) + (((__b) - (__a))*(__t)))

static uint32_t color_lerp(float ft, uint32_t c1, uint32_t c2) {
#if 0
	uint8_t t = (ft * 255);
   	uint32_t maskRB = 0xFF00FF;  // Mask for Red & Blue channels
	uint32_t maskG  = 0x00FF00;  // Mask for Green channel
	uint32_t maskA  = 0xFF000000; // Mask for Alpha channel

    // Interpolate Red & Blue
    uint32_t rb = ((((c2 & maskRB) - (c1 & maskRB)) * t) >> 8) + (c1 & maskRB);
   
    // Interpolate Green
    uint32_t g  = ((((c2 & maskG) - (c1 & maskG)) * t) >> 8) + (c1 & maskG);

    // Interpolate Alpha
    uint32_t a  = ((((c2 & maskA) >> 24) - ((c1 & maskA) >> 24)) * t) >> 8;
    a = (a + (c1 >> 24)) << 24;  // Shift back into position

    return (a & maskA) | (rb & maskRB) | (g & maskG);
#endif
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

static inline void perspdiv(pvr_vertex_t *v, float w)
{
	float invw = approx_recip(w);
	float x = v->x * invw;
	float y = v->y * invw;

#if RENDER_USE_FSAA
	x = 640.0f + (640.0f * x);
#else
	x = 320.0f + (320.0f * x);
#endif
	y = 240.0f - (240.0f * y);

	v->x = x;
	v->y = y;
	v->z = invw;
}

void render_noclip_quad(uint16_t texture_index) {
	float w0,w1,w2,w3;
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

extern int menu_overlay;

void render_quad(uint16_t texture_index) {
	float w0,w1,w2,w3,w4;

	int notex = (texture_index == RENDER_NO_TEXTURE);

	mat_trans_single3_nodivw(vs[0].x,vs[0].y,vs[0].z,w0);
	mat_trans_single3_nodivw(vs[1].x,vs[1].y,vs[1].z,w1);
	mat_trans_single3_nodivw(vs[2].x,vs[2].y,vs[2].z,w2);
	mat_trans_single3_nodivw(vs[3].x,vs[3].y,vs[3].z,w3);

	uint8_t cl0, cl1, cl2, cl3;

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

	if ((cl0 | cl1 | cl2 | cl3) != 0x3f) {
		return;
	}

	uint32_t vismask = ((vs[0].z >= -w0) | ((vs[1].z >= -w1) << 1) | ((vs[2].z >= -w2) << 2) | ((vs[3].z >= -w3) << 3));
	vs[2].flags = PVR_CMD_VERTEX;
	int sendverts = 4;

	if (vismask == 15) {
		goto quad_sendit;
	} else {
		// if you want to literally see which quads get near-z clipped
		//return;
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

		break;

	// quad 0 + 1 + 2 visible
	case 7:
		sendverts = 5;

		nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
		w4 = wout;
		nearz_clip(&vs[1], &vs[3], &vs[3], w1, w3);
		w3 = wout;

		vs[3].flags = PVR_CMD_VERTEX;
//		vs[4].flags = PVR_CMD_VERTEX_EOL;

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

		break;

	// quad 0 + 1 + 3 visible
	case 11:
		sendverts = 5;

		nearz_clip(&vs[2], &vs[3], &vs[4], w2, w3);
		w4 = wout;
		nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
		w2 = wout;

		vs[3].flags = PVR_CMD_VERTEX;
//		vs[4].flags = PVR_CMD_VERTEX_EOL;

		break;

	// quad 2 + 3 visible
	case 12:
		nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
		w0 = wout;
		nearz_clip(&vs[1], &vs[3], &vs[1], w1, w3);
		w1 = wout;

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
		break;
		}
	}
quad_sendit:

	uint8_t a0 = ((vs[0].argb >> 24) & 0x000000ff);
	uint8_t oa0 = a0;

	if (save.fade && !no_fade) {
		if (a0 == 255) {
			float zw0 = 1.0f - (vs[0].z * approx_recip(w0));
			float zw1 = 1.0f - (vs[1].z * approx_recip(w1));
			float zw2 = 1.0f - (vs[2].z * approx_recip(w2));

			uint8_t a1 = ((vs[1].argb >> 24) & 0x000000ff);
			uint8_t a2 = ((vs[2].argb >> 24) & 0x000000ff);

			a0 = clamp(a0 * zw0 * FARPLANESCALE, 0, 255);
			vs[0].argb = (vs[0].argb & 0x00ffffff) | (a0 << 24);
			a1 = clamp(a1 * zw1 * FARPLANESCALE, 0, 255);
			vs[1].argb = (vs[1].argb & 0x00ffffff) | (a1 << 24);
			a2 = clamp(a2 * zw2 * FARPLANESCALE, 0, 255);
			vs[2].argb = (vs[2].argb & 0x00ffffff) | (a2 << 24);
			if (sendverts > 3) {
				float zw3 = 1.0f - (vs[3].z * approx_recip(w3));

				uint8_t a3 = ((vs[3].argb >> 24) & 0x000000ff);

				a3 = clamp(a3 * zw3 * FARPLANESCALE, 0, 255);
				vs[3].argb = (vs[3].argb & 0x00ffffff) | (a3 << 24);
			}
			if (sendverts == 5) {
				float zw4 = 1.0f - (vs[4].z * approx_recip(w4));

				uint8_t a4 = ((vs[4].argb >> 24) & 0x000000ff);

				a4 = clamp(a4 * zw4 * FARPLANESCALE, 0, 255);
				vs[4].argb = (vs[4].argb & 0x00ffffff) | (a4 << 24);
			}
		}
	}

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);
	if (sendverts > 3)
		perspdiv(&vs[3], w3);
	if (sendverts == 5)
		perspdiv(&vs[4], w4);

	// this is a fix for the shields being drawn in a way that messes with depth writing
	// and was making flames disappear
	int prev_dep = dep_en;
	if (oa0 == 48) {
		render_set_depth_write(false);
		vs[0].z += 0.00005f;
		vs[1].z += 0.00005f;
		vs[2].z += 0.00005f;
		if (sendverts > 3)
			vs[3].z += 0.00005f;
		if (sendverts == 5)
			vs[4].z += 0.00005f;
	}

	if (vs[0].oargb < 3) {
		if (last_index != texture_index) {
			last_index = texture_index;

			if (cur_mode != last_mode[texture_index]) {
				last_mode[texture_index] = cur_mode;

				if (texture_index < TEXTURES_MAX) {
					update_header(texture_index, vs[0].oargb);
				}
			}

			if(__builtin_expect(notex,0)) {
				pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
			} else {
				pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
			}
		}

		pvr_prim(vs, sendverts * 32);

		// shield cleanup
		if (oa0 == 48)
			render_set_depth_write(prev_dep);
	} else if (vs[0].oargb > 2) {
		int prev_cull = cull_en;
		render_blend_mode_t prev_blend = blend_mode;
		render_set_blend_mode(RENDER_BLEND_STUPID);
		render_set_cull_backface(true);
		last_index = texture_index;
		last_mode[texture_index] = cur_mode;

		update_header(texture_index, vs[0].oargb);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, sendverts * 32);
		render_set_blend_mode(RENDER_BLEND_SPECIAL);
		last_mode[texture_index] = cur_mode;
		update_header(texture_index, vs[0].oargb);

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

extern int shields_active;

void render_tri(uint16_t texture_index) {
	int notex = (texture_index == RENDER_NO_TEXTURE);
	float w0,w1,w2,w3;

	mat_trans_single3_nodivw(vs[0].x,vs[0].y,vs[0].z,w0);
	mat_trans_single3_nodivw(vs[1].x,vs[1].y,vs[1].z,w1);
	mat_trans_single3_nodivw(vs[2].x,vs[2].y,vs[2].z,w2);

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

	if ((cl0 | cl1 | cl2) != 0x3f) {
		return;
	}

	uint32_t vismask = (vs[0].z >= -w0) | ((vs[1].z >= -w1) << 1) | ((vs[2].z >= -w2) << 2);
	int usespare = 0;
	vs[2].flags = PVR_CMD_VERTEX_EOL;

	if (vismask == 7) {
		goto tri_sendit;
	} else {
		// if you want to literally see which tris get near-z clipped
		//return;
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
			usespare = 1;
			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			break;
		case 4:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[1], w1, w2);
			w1 = wout;
			break;
		case 5:
			usespare = 1;
			nearz_clip(&vs[1], &vs[2], &vs[3], w1, w2);
			w3 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			break;
		case 6:
			usespare = 1;
			memcpy32(&vs[3], &vs[2], 32);
			w3 = w2;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			break;
		}
	}
tri_sendit:
	uint8_t a0 = ((vs[0].argb >> 24) & 0x000000ff);
	uint8_t oa0 = a0;

	if (save.fade && !no_fade) {
		if (a0 == 255) {
			float zw0 = 1.0f - (vs[0].z * approx_recip(w0));
			float zw1 = 1.0f - (vs[1].z * approx_recip(w1));
			float zw2 = 1.0f - (vs[2].z * approx_recip(w2));

			uint8_t a1 = ((vs[1].argb >> 24) & 0x000000ff);
			uint8_t a2 = ((vs[2].argb >> 24) & 0x000000ff);

			a0 = clamp(a0 * zw0 * FARPLANESCALE, 0, 255);
			vs[0].argb = (vs[0].argb & 0x00ffffff) | (a0 << 24);
			a1 = clamp(a1 * zw1 * FARPLANESCALE, 0, 255);
			vs[1].argb = (vs[1].argb & 0x00ffffff) | (a1 << 24);
			a2 = clamp(a2 * zw2 * FARPLANESCALE, 0, 255);
			vs[2].argb = (vs[2].argb & 0x00ffffff) | (a2 << 24);
			if (usespare) {
				float zw3 = 1.0f - (vs[3].z * approx_recip(w3));

				uint8_t a3 = ((vs[3].argb >> 24) & 0x000000ff);

				a3 = clamp(a3 * zw3 * FARPLANESCALE, 0, 255);
				vs[3].argb = (vs[3].argb & 0x00ffffff) | (a3 << 24);
			}
		}
	}

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);
	if (usespare) {
		vs[2].flags = PVR_CMD_VERTEX;
		perspdiv(&vs[3], w3);
	}

	// this is a fix for the shields being drawn in a way that messes with depth writing
	// and was making flames disappear
	int prev_dep = dep_en;
	if (oa0 == 48) {
		render_set_depth_write(false);
		vs[0].z += 0.00005f;
		vs[1].z += 0.00005f;
		vs[2].z += 0.00005f;
		if (usespare)
			vs[3].z += 0.00005f;
	}

	// don't do anything header-related if we're on the same texture as the last call
	// automatic saving every time quads are pushed as two tris
	// and savings in plenty of other cases
	if (vs[0].oargb < 2) {
		if (last_index != texture_index) {
			last_index = texture_index;

			if (cur_mode != last_mode[texture_index]) {
				last_mode[texture_index] = cur_mode;

				if (texture_index < TEXTURES_MAX)
					update_header(texture_index, vs[0].oargb);
			}

			if(__builtin_expect(notex,0))
				pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
			else
				pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));
		}


		pvr_prim(vs, 3 * 32);
		if (usespare)
			pvr_prim(&vs[3], 32);

		// shield cleanup
		if (oa0 == 48)
			render_set_depth_write(prev_dep);
	} else {
		render_blend_mode_t prev_blend = blend_mode;
		int prev_cull = cull_en;
		if (!shields_active)
			render_set_depth_write(false);
		render_set_cull_backface(true);
		render_set_blend_mode(RENDER_BLEND_NORMAL);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index, vs[0].oargb);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, 3 * 32);
		if (usespare)
			pvr_prim(&vs[3], 32);

		render_set_blend_mode(RENDER_BLEND_LIGHTER);

		last_mode[texture_index] = cur_mode;
		update_header(texture_index, vs[0].oargb);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex, sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index], sizeof(pvr_poly_hdr_t));

		vs[0].z += 0.000025f;
		vs[1].z += 0.000025f;
		vs[2].z += 0.000025f;
		if (usespare)
			vs[3].z += 0.000025f;

		pvr_prim(vs, 3 * 32);
		if (usespare)
			pvr_prim(&vs[3], 32);

		render_set_cull_backface(prev_cull);
		render_set_blend_mode(prev_blend);
		render_set_depth_write(prev_dep);
		if (!shields_active)
			render_set_depth_write(prev_dep);
		last_index = texture_index;
	}
}


void render_push_sprite(vec3_t pos, vec2i_t size, uint32_t lcol/* or */, uint16_t texture_index) {
	screen_2d_z += 0.001f;
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
	vs[0].u = 1.0f * rpw;
	vs[0].v = 1.0f * rph;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

//	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = p2.x;
	vs[1].y = p2.y;
	vs[1].z = p2.z;
	vs[1].u = (t->size.x - 1.0f) * rpw;
	vs[1].v = 1.0f * rph;
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

	screen_2d_z += 0.001f;

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

uint16_t tmpstore[512*512];

uint16_t render_texture_create(uint32_t tw, uint32_t th, uint16_t *pixels) {
	uint16_t texture_index = textures_len++;

	if (texture_index != 0) {
		int wp2 = np2(tw);
		int hp2 = np2(th);

		// there's something weird with textures of the bottom of the right wing of every ship
		// and the ring through the top of the stopwatch in the options menu
		// i dont really know what is going on there, but this is a color that doesnt look ridiculous
		// for any of those contexts
		for(int i=0;i<512*512;i++) tmpstore[i] = 0xBDEF;

		ptrs[texture_index] = pvr_mem_malloc(wp2 * hp2 * 2);
		// if we ran out of VRAM (we don't, but if we did), handle it by just using color poly header
		if (ptrs[texture_index == 0])
			goto createbail;

		textures[texture_index] = (render_texture_t){ {wp2, hp2}, {tw, th} };

		for (uint32_t h=0;h<th;h++) {
			for(uint32_t w=0;w<tw;w++) {
				tmpstore[(h*wp2) + w] = pixels[(h*tw) + w];
			}
		}

		pvr_txr_load_ex(tmpstore, ptrs[texture_index], wp2, hp2, PVR_TXRLOAD_16BPP);
	}

	compile_header(texture_index);

createbail:
	if (ptrs[texture_index] == 0) {
		chdr[texture_index] = &chdr_notex;
	}

	return texture_index;
}

vec2i_t render_texture_size(uint16_t texture_index) {
//	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	return textures[texture_index].size;
}

vec2i_t render_texture_padsize(uint16_t texture_index) {
//	error_if(texture_index >= textures_len, "Invalid texture %d", texture_index);
	return textures[texture_index].offset;
}

void render_texture_replace_pixels(int16_t texture_index, uint16_t *pixels) {
	// only used by pl_mpeg for intro video
}

uint16_t render_textures_len(void) {
	return textures_len;
}

void render_textures_reset(uint16_t len) {
	//error_if(len > textures_len, "Invalid texture reset len %d >= %d", len, textures_len);
	for (uint16_t curlen = len;curlen<textures_len;curlen++) {
		if (ptrs[curlen]) {
			pvr_mem_free(ptrs[curlen]);
			ptrs[curlen] = 0;

			free(chdr[curlen]);
			chdr[curlen] = NULL;
		}

		last_mode[curlen] = 0;
	}

	textures_len = len;

	// Clear completely and recreate the default white texture
	if (len == 0) {
		uint16_t white_pixels[4] = {0xffff, 0xffff, 0xffff, 0xffff};
		RENDER_NO_TEXTURE = render_texture_create(2, 2, white_pixels);
		return;
	}
}

void render_textures_dump(const char *path) {
#if 0
	// nope
	char tdfn[256];
	for (uint16_t i=1;i<textures_len;i++) {
		sprintf(tdfn, "/pc/tex%04x.ppm", i);
		lowmem_screen_shot(i, tdfn);
	}
#endif
}
