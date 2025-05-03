/**
 * WARNING
 * 
 * You will see many things in here that look "bad".
 * 
 * You will be tempted to change them for consistency or "efficiency."
 * 
 * When you do, you are going to lose a bunch of rendering throughput.
 * 
 * Something, or many things, are sensitive to the code generation and memory layout after
 * compiling `-O3` with LTO enabled.
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

#if RENDER_USE_FSAA
static float shakex = 640.0f;
#else
static float shakex = 320.0f;
#endif
static float shakey = 240.0f;
static float shakeCount = 0.0f;

#define NEAR_PLANE 16.0f
#define FAR_PLANE (RENDER_FADEOUT_FAR)

#define TEXTURES_MAX 256

#define RENDER_STATEMAP(w,t,c,bl) (((int)(w) << 1) | ((int)(t) << 2) | ((int)(bl) << 4))

extern void memcpy32(const void *dst, const void *src, size_t s);

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

// thanks @FalcoGirgis
inline static void fast_mat_store(matrix_t *mtx) {
    asm volatile(
        R"(
            fschg
            add            #64-8,%[mtx]
            fmov.d    xd14,@%[mtx]
            add            #-32,%[mtx]
            pref    @%[mtx]
            add         #32,%[mtx]
            fmov.d    xd12,@-%[mtx]
            fmov.d    xd10,@-%[mtx]
            fmov.d    xd8,@-%[mtx]
            fmov.d    xd6,@-%[mtx]
            fmov.d    xd4,@-%[mtx]
            fmov.d    xd2,@-%[mtx]
            fmov.d    xd0,@-%[mtx]
            fschg
        )"
        : [mtx] "+&r" (mtx), "=m" (*mtx)
        :
        :
    );
}

typedef struct {
	// 0 - 7
	vec2i_t offset;
	// 8 - 15
	vec2i_t size;
} render_texture_t;

float screen_2d_z = -1.0f;

pvr_dr_state_t dr_state;
pvr_vertex_t vs[5];
void __attribute__((aligned(32))) *ptrs[TEXTURES_MAX] = {0};
uint8_t __attribute__((aligned(32))) last_mode[TEXTURES_MAX] = {0};

uint16_t RENDER_NO_TEXTURE;
const uint16_t HUD_NO_TEXTURE = 65535;

vec2i_t screen_size;

static mat4_t __attribute__((aligned(32))) projection_mat = mat4_identity();
static mat4_t __attribute__((aligned(32))) sprite_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) view_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) mvp_mat = mat4_identity();
mat4_t __attribute__((aligned(32))) vp_mat;

static render_texture_t __attribute__((aligned(32))) textures[TEXTURES_MAX];
static uint32_t textures_len = 0;

static uint8_t __attribute__((aligned(32))) OGNOFILT[TEXTURES_MAX] = {0};
static pvr_poly_hdr_t __attribute__((aligned(32))) *chdr[TEXTURES_MAX][2] = {0};
pvr_poly_hdr_t chdr_notex[2];
pvr_poly_hdr_t hud_hdr;

global_render_state_t __attribute__((aligned(32))) render_state;

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
		uint32_t *hp = (uint32_t *)chdr[texture_index][0];

		header1 = hp[1];
		header2 = hp[2];

		// depth write
		if (render_state.dep_en)
			header1 &= ~(PVR_TA_PM1_DEPTHWRITE_MASK);
		else
			header1 = (header1 & ~(PVR_TA_PM1_DEPTHWRITE_MASK)) | (PVR_DEPTHWRITE_DISABLE << PVR_TA_PM1_DEPTHWRITE_SHIFT);

		// depth test
		if (render_state.test_en)
			header1 = (header1 & ~(PVR_TA_PM1_DEPTHCMP_MASK)) | (PVR_DEPTHCMP_GREATER << PVR_TA_PM1_DEPTHCMP_SHIFT);
		else
			header1 = (header1 & ~(PVR_TA_PM1_DEPTHCMP_MASK)) | (PVR_DEPTHCMP_ALWAYS << PVR_TA_PM1_DEPTHCMP_SHIFT);

		// culling off
		//header1 &= ~(PVR_TA_PM1_CULLING_MASK);

		// clear blending
		header2 &= ~(PVR_TA_PM2_SRCBLEND_MASK | PVR_TA_PM2_DSTBLEND_MASK);

		// normal or brighter
		if (render_state.blend_mode == RENDER_BLEND_NORMAL) {
			header2 |= (PVR_BLEND_SRCALPHA << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_INVSRCALPHA << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (render_state.blend_mode == RENDER_BLEND_LIGHTER) {
			header2 |= (PVR_BLEND_SRCALPHA << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ONE << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (render_state.blend_mode == RENDER_BLEND_SPECIAL) {
			// srcalpha brighter but bleeds
			header2 |= (PVR_BLEND_DESTCOLOR << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ONE << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (render_state.blend_mode == RENDER_BLEND_STUPID) {
			header2 |= (PVR_BLEND_ONE << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ZERO << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (render_state.blend_mode == RENDER_BLEND_SPECIAL2) {
			header2 |= (PVR_BLEND_ONE << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_ONE << PVR_TA_PM2_DSTBLEND_SHIFT);
		}
		else if (render_state.blend_mode == RENDER_BLEND_BRIGHTBRIGHT) {
			header2 |= (PVR_BLEND_ONE << PVR_TA_PM2_SRCBLEND_SHIFT) | (PVR_BLEND_INVSRCALPHA << PVR_TA_PM2_DSTBLEND_SHIFT);
		}

		hp[1] = header1;
		hp[2] = header2;

		// culling on
		header1 |= (PVR_CULLING_CCW << PVR_TA_PM1_CULLING_SHIFT);
		hp = (uint32_t *)chdr[texture_index][1];
		hp[1] = header1;
		hp[2] = header2;
}

void compile_header(uint16_t texture_index) {
	pvr_poly_cxt_t ccxt;
	render_texture_t *t = &textures[texture_index];
	uint32_t filtering = (render_state.LOAD_UNFILTERED || !save.filter) ? PVR_FILTER_NONE : PVR_FILTER_BILINEAR;
	OGNOFILT[texture_index] = render_state.LOAD_UNFILTERED;
	if ((texture_index != 0)) {
		chdr[texture_index][0] = memalign(32, sizeof(pvr_poly_hdr_t)*2);
		chdr[texture_index][1] = (pvr_poly_hdr_t *)((uintptr_t)chdr[texture_index][0] + sizeof(pvr_poly_hdr_t));

		if (!render_state.load_OP) {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);
		} else {
			pvr_poly_cxt_txr(&ccxt, PVR_LIST_OP_POLY, PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_TWIDDLED, t->offset.x, t->offset.y, ptrs[texture_index], filtering);
		}

		ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		//if (render_state.load_OP)
		//	ccxt.txr.env = PVR_TXRENV_DECAL;
		ccxt.depth.write = PVR_DEPTHWRITE_DISABLE;
		ccxt.depth.comparison = PVR_DEPTHCMP_NEVER;
		pvr_poly_compile(chdr[texture_index][1], &ccxt);

		ccxt.gen.culling = PVR_CULLING_NONE;
		pvr_poly_compile(chdr[texture_index][0], &ccxt);
	} else {
		pvr_poly_cxt_col(&ccxt, PVR_LIST_TR_POLY);
		ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		pvr_poly_compile(&chdr_notex[1], &ccxt);

		ccxt.gen.specular = PVR_SPECULAR_ENABLE;
		ccxt.gen.culling = PVR_CULLING_NONE;
		pvr_poly_compile(&chdr_notex[0], &ccxt);
	}
}

void render_init(void) {
	pvr_poly_cxt_t ccxt;
	pvr_init(&pvr_params);

	// 3mb VRAM block for glDC allocator
	pvr_ptr_t block = pvr_mem_malloc((1048576*3)+2048);
	if (-1 == alloc_init((void*)block, (1048576*3)))
		exit(-1);

	render_state.in_menu = 0;
	render_state.in_race = 0;
	render_state.last_index = 256;
	render_state.cull_en = 1;
	render_state.dep_en = 1;
	render_state.test_en = 1;
	render_state.blend_mode = RENDER_BLEND_NORMAL;
	pvr_set_bg_color(0.0f,0.0f,0.0f);

	render_state.cur_mode = RENDER_STATEMAP(1,1,1,0);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].oargb = 0;
	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].oargb = 0;
	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[2].oargb = 0;
	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].oargb = 0;
	vs[4].flags = PVR_CMD_VERTEX_EOL;
	vs[4].oargb = 0;

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
	float nf = /* -approx_recip */1.0f / (NEAR_PLANE - farval);
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
	float fov = (73.75f / 180.0f) * F_PI;
	float f = 1.0f / tanf(fov * 0.5f);
	float nf = 1.0f / (NEAR_PLANE - 96000.0f);

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

	if (render_state.in_menu || render_state.in_race)
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
/* 	for (int i=0;i<4;i++) {
		for (int j=0;j<4;j++) {
			rot_sprite_mat.cols[i][j] = sprite_mat.cols[j][i];
		}
	} */
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

	fast_mat_load(&mvp_mat.cols);
}

extern void fast_mat_apply(const matrix_t *mat);

void render_set_model_mat(mat4_t *m) {
	mat_load_apply(&vp_mat.cols, &view_mat.cols);
	fast_mat_apply(&m->cols);
}

void render_set_model_ident(void) {
	mat_load_apply(&vp_mat.cols, &view_mat.cols);
}

void render_set_depth_write(bool enabled) {
	if ((int)enabled != render_state.dep_en) {
		render_state.dep_en = enabled;
		render_state.cur_mode = RENDER_STATEMAP(render_state.dep_en,render_state.test_en,render_state.cull_en,render_state.blend_mode);
	}
}

void render_set_depth_test(bool enabled) {
	if ((int)enabled != render_state.test_en) {
		render_state.test_en = enabled;
		render_state.cur_mode = RENDER_STATEMAP(render_state.dep_en,render_state.test_en,render_state.cull_en,render_state.blend_mode);
	}
}

void render_set_depth_offset(float offset) {
	; //
}

void render_set_screen_position(vec2_t pos) {
	; //
}

void render_set_blend_mode(render_blend_mode_t new_mode) {
	if (new_mode != render_state.blend_mode) {
		render_state.blend_mode = new_mode;
		render_state.cur_mode = RENDER_STATEMAP(render_state.dep_en,render_state.test_en,render_state.cull_en,render_state.blend_mode);
	}
}

int cull_dirty = 0;

void render_set_cull_backface(bool enabled) {
	if (enabled != render_state.cull_en) {
		render_state.cull_en = enabled;
		render_state.cur_mode = RENDER_STATEMAP(render_state.dep_en,render_state.test_en,render_state.cull_en,render_state.blend_mode);
		render_state.cull_dirty = 1;
	}
}

static float wout;
#define cliplerp(__a, __b, __t) ((__a) + (((__b) - (__a))*(__t)))

static void  __attribute__((noinline)) nearz_clip(pvr_vertex_t *v0, pvr_vertex_t *v1, pvr_vertex_t *outv, float w0, float w1) {
#if 0
	const float d0 = w0 + v0->z;
	const float d1 = w1 + v1->z;
	const float d1subd0 = d1 - d0;
	const float t = fabsf(d0 * approx_recip(d1subd0));
#else
	const float t = fabsf(v0->z * approx_recip(v1->z - v0->z));
#endif
	wout = cliplerp(w0, w1, t);
	outv->x = cliplerp(v0->x, v1->x, t);
	outv->y = cliplerp(v0->y, v1->y, t);
	outv->z = cliplerp(v0->z, v1->z, t);
	outv->u = cliplerp(v0->u, v1->u, t);
	outv->v = cliplerp(v0->v, v1->v, t);
	// these won't matter again until lighting is added
	outv->argb = v0->argb;//color_lerp(t, v0->argb, v1->argb);
//	if (v0->oargb)
//		outv->oargb = v0->oargb;//color_lerp(t, v0->oargb, v1->oargb);
}

void SetShake(float duration) {
	shakeCount = (duration / 30.0f);
}

void ShakeScreen(void) {
	if(shakeCount > 0.0f) {
		// phoboslab/wipeout-rewrite fca01a0
		float s = 0.5f * shakeCount * 45.0f;
#if RENDER_USE_FSAA
		shakex = 640.0f + rand_float(-s, s) * 2.0f;
#else
		shakex = 320.0f + rand_float(-s, s);
#endif
		shakey = 240.0f + rand_float(-s, s);

		shakeCount -= system_tick();
	}
	else {
#if RENDER_USE_FSAA
		shakex = 640.0f;
#else
		shakex = 320.0f;
#endif
		shakey = 240.0f;
		shakeCount = 0.0f;
	}
}

static inline void perspdiv(pvr_vertex_t *v, float w) {
	const float invw = approx_recip(w);
	float x = v->x * invw;
	float y = v->y * invw;

#if RENDER_USE_FSAA
	x = shakex + (640.0f * x);
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

void  __attribute__((noinline)) render_quad(uint16_t texture_index) {
	// ((vs[0].z >= -w0) | ((vs[1].z >= -w1) << 1) | ((vs[2].z >= -w2) << 2) | ((vs[3].z >= -w3) << 3))
	uint32_t vismask;
	uint8_t cl0, cl1, cl2, cl3;
	float w0,w1,w2,w3,w4;

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

	vismask = (cl0 & 0x10) >> 4;

	cl1 = !(vs[1].z >  w1);
	cl1 = (cl1 << 1) | !(vs[1].z < -w1);
	cl1 = (cl1 << 1) | !(vs[1].y >  w1);
	cl1 = (cl1 << 1) | !(vs[1].y < -w1);
	cl1 = (cl1 << 1) | !(vs[1].x >  w1);
	cl1 = (cl1 << 1) | !(vs[1].x < -w1);

	vismask |= (cl1 & 0x10) >> 3;

	cl2 = !(vs[2].z >  w2);
	cl2 = (cl2 << 1) | !(vs[2].z < -w2);
	cl2 = (cl2 << 1) | !(vs[2].y >  w2);
	cl2 = (cl2 << 1) | !(vs[2].y < -w2);
	cl2 = (cl2 << 1) | !(vs[2].x >  w2);
	cl2 = (cl2 << 1) | !(vs[2].x < -w2);

	vismask |= (cl2 & 0x10) >> 2;

	cl3 = !(vs[3].z >  w3);
	cl3 = (cl3 << 1) | !(vs[3].z < -w3);
	cl3 = (cl3 << 1) | !(vs[3].y >  w3);
	cl3 = (cl3 << 1) | !(vs[3].y < -w3);
	cl3 = (cl3 << 1) | !(vs[3].x >  w3);
	cl3 = (cl3 << 1) | !(vs[3].x < -w3);

	vismask |= (cl3 & 0x10) >> 1;

	if ((cl0 | cl1 | cl2 | cl3) != 0x3f)
		return;

	int sendverts = 4;
	int quad_is_pad = vs[0].oargb > 2;
	vs[0].oargb = 0;

	if (vismask == 15) {
		perspdiv(&vs[3], w3);
		goto quad_sendit;
	} else {
		switch (vismask)
		{
		// quad only 0 visible
		case 1:
			sendverts = 3;
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad only 1 visible
		case 2:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
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
	if (!quad_is_pad) {
		if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
			// ^-- both of these need to be checked at top level, not one with one nested
			render_state.last_index = texture_index;

			last_mode[texture_index] = render_state.cur_mode;

			update_header(texture_index);

			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
		} else if (__builtin_expect(render_state.cull_dirty,0)) {
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

			render_state.cull_dirty = 0;
		}

		pvr_prim(vs, sendverts * 32);
	} else {
		// boost/item pads use blending to be bright enough/glow
		render_blend_mode_t prev_blend = render_state.blend_mode;
		render_state.last_index = texture_index;

		// base quad
		render_set_blend_mode(RENDER_BLEND_STUPID);

		last_mode[texture_index] = render_state.cur_mode;
		update_header(texture_index);

		pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		pvr_prim(vs, sendverts * 32);

		// blended quad
		render_set_blend_mode(RENDER_BLEND_SPECIAL);

		last_mode[texture_index] = render_state.cur_mode;
		update_header(texture_index);

		pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		// slight offset up and toward near plane
		for (int i=0;i<sendverts;i++) {
			vs[i].y += 0.000005f;
			vs[i].z += 0.000005f;
		}

		pvr_prim(vs, sendverts * 32);

		// restore blend mode that was set before any of this
		render_set_blend_mode(prev_blend);
	}
}

void  __attribute__((noinline)) render_tri(uint16_t texture_index) {
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

	if (vismask == 7) {
		goto tri_sendit;
	} else {
		switch (vismask)
		{
		// triangle only 0 visible
		case 1:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			break;
		// triangle only 1 visible
		case 2:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[2], w1, w2);
			w2 = wout;

			break;
		// triangle 0 + 1 visible
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
		// triangle only 2 visible
		case 4:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[1], w1, w2);
			w1 = wout;

			break;
		// triangle 0 + 2 visible
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
		// triangle 1 + 2 visible
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
	if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
		// ^-- both of these need to be checked at top level, not one with one nested
		render_state.last_index = texture_index;

		last_mode[texture_index] = render_state.cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
	} else if (__builtin_expect(cull_dirty,0)) {
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		cull_dirty = 0;
	}

	pvr_prim(vs, sendverts * 32);
}

void  __attribute__((noinline)) render_quad_noxform(uint16_t texture_index, float *w) {
	// ((vs[0].z >= -w0) | ((vs[1].z >= -w1) << 1) | ((vs[2].z >= -w2) << 2) | ((vs[3].z >= -w3) << 3))
	uint32_t vismask;
	uint8_t cl0, cl1, cl2, cl3;
	float w0,w1,w2,w3,w4;
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

	vismask = (cl0 & 0x10) >> 4;

	cl1 = !(vs[1].z >  w1);
	cl1 = (cl1 << 1) | !(vs[1].z < -w1);
	cl1 = (cl1 << 1) | !(vs[1].y >  w1);
	cl1 = (cl1 << 1) | !(vs[1].y < -w1);
	cl1 = (cl1 << 1) | !(vs[1].x >  w1);
	cl1 = (cl1 << 1) | !(vs[1].x < -w1);

	vismask |= (cl1 & 0x10) >> 3;

	cl2 = !(vs[2].z >  w2);
	cl2 = (cl2 << 1) | !(vs[2].z < -w2);
	cl2 = (cl2 << 1) | !(vs[2].y >  w2);
	cl2 = (cl2 << 1) | !(vs[2].y < -w2);
	cl2 = (cl2 << 1) | !(vs[2].x >  w2);
	cl2 = (cl2 << 1) | !(vs[2].x < -w2);

	vismask |= (cl2 & 0x10) >> 2;

	cl3 = !(vs[3].z >  w3);
	cl3 = (cl3 << 1) | !(vs[3].z < -w3);
	cl3 = (cl3 << 1) | !(vs[3].y >  w3);
	cl3 = (cl3 << 1) | !(vs[3].y < -w3);
	cl3 = (cl3 << 1) | !(vs[3].x >  w3);
	cl3 = (cl3 << 1) | !(vs[3].x < -w3);

	vismask |= (cl3 & 0x10) >> 1;

	if ((cl0 | cl1 | cl2 | cl3) != 0x3f)
		return;

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

			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			vs[2].flags = PVR_CMD_VERTEX_EOL;

			break;

		// quad only 1 visible
		case 2:
			sendverts = 3;

			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
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

	if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
		// ^-- both of these need to be checked at top level, not one with one nested
		render_state.last_index = texture_index;

		last_mode[texture_index] = render_state.cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
	} else if (__builtin_expect(cull_dirty,0)) {
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		cull_dirty = 0;
	}

	pvr_prim(vs, sendverts * 32);
}

void  __attribute__((noinline)) render_tri_noxform(uint16_t texture_index, float *w) {
	// ((vs[0].z >= -w0) | ((vs[1].z >= -w1) << 1) | ((vs[2].z >= -w2) << 2) | ((vs[3].z >= -w3) << 3));
	uint32_t vismask;
	uint8_t cl0, cl1, cl2;
	uint8_t pad;
	float w0,w1,w2,w3;
	int notex = (texture_index == RENDER_NO_TEXTURE);
	(void)pad;
	w0 = w[0];
	w1 = w[1];
	w2 = w[2];

	cl0 = !(vs[0].z >  w0);
	cl0 = (cl0 << 1) | !(vs[0].z < -w0);
	cl0 = (cl0 << 1) | !(vs[0].y >  w0);
	cl0 = (cl0 << 1) | !(vs[0].y < -w0);
	cl0 = (cl0 << 1) | !(vs[0].x >  w0);
	cl0 = (cl0 << 1) | !(vs[0].x < -w0);

	vismask = (cl0 & 0x10) >> 4;

	cl1 = !(vs[1].z >  w1);
	cl1 = (cl1 << 1) | !(vs[1].z < -w1);
	cl1 = (cl1 << 1) | !(vs[1].y >  w1);
	cl1 = (cl1 << 1) | !(vs[1].y < -w1);
	cl1 = (cl1 << 1) | !(vs[1].x >  w1);
	cl1 = (cl1 << 1) | !(vs[1].x < -w1);

	vismask |= (cl1 & 0x10) >> 3;

	cl2 = !(vs[2].z >  w2);
	cl2 = (cl2 << 1) | !(vs[2].z < -w2);
	cl2 = (cl2 << 1) | !(vs[2].y >  w2);
	cl2 = (cl2 << 1) | !(vs[2].y < -w2);
	cl2 = (cl2 << 1) | !(vs[2].x >  w2);
	cl2 = (cl2 << 1) | !(vs[2].x < -w2);

	vismask |= (cl2 & 0x10) >> 2;

	if ((cl0 | cl1 | cl2) != 0x3f)
		return;

	int sendverts = 3;

	if (vismask == 7) {
		goto tri_sendit;
	} else {
		switch (vismask)
		{
		// triangle only 0 visible
		case 1:
			nearz_clip(&vs[0], &vs[1], &vs[1], w0, w1);
			w1 = wout;
			nearz_clip(&vs[0], &vs[2], &vs[2], w0, w2);
			w2 = wout;

			break;
		// triangle only 1 visible
		case 2:
			nearz_clip(&vs[0], &vs[1], &vs[0], w0, w1);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[2], w1, w2);
			w2 = wout;

			break;
		// triangle 0 + 1 visible
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
		// triangle only 2 visible
		case 4:
			nearz_clip(&vs[0], &vs[2], &vs[0], w0, w2);
			w0 = wout;
			nearz_clip(&vs[1], &vs[2], &vs[1], w1, w2);
			w1 = wout;

			break;
		// triangle 0 + 2 visible
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
		// triangle 1 + 2 visible
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

//	render_set_blend_mode(RENDER_BLEND_NORMAL);
	if ((vs[0].argb & 0xff000000) < 0xff000000) {
		if ((vs[0].argb & 0xff000000) != 0x60000000)
			render_set_blend_mode(RENDER_BLEND_BRIGHTBRIGHT);
		else
			render_set_blend_mode(RENDER_BLEND_NORMAL);
	} else {
		render_set_blend_mode(RENDER_BLEND_NORMAL);
	}

	// don't do anything header-related if we're on the same texture or render mode as the last call
	if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
		// ^-- both of these need to be checked at top level, not one with one nested
		render_state.last_index = texture_index;

		last_mode[texture_index] = render_state.cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
	} else if (__builtin_expect(render_state.cull_dirty,0)) {
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		render_state.cull_dirty = 0;
	}

	pvr_prim(vs, sendverts * 32);
}

void __attribute__((noinline)) render_quad_noxform_noclip(uint16_t texture_index, float *w) {
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

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);
	perspdiv(&vs[3], w3);

	if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
		// ^-- both of these need to be checked at top level, not one with one nested
		render_state.last_index = texture_index;

		last_mode[texture_index] = render_state.cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
	} else if (__builtin_expect(render_state.cull_dirty,0)) {
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		render_state.cull_dirty = 0;
	}

	pvr_prim(vs, 128);
}


void  __attribute__((noinline)) render_tri_noxform_noclip(uint16_t texture_index, float *w) {
	float w0,w1,w2;
 	uint8_t cl0, cl1, cl2;
	uint8_t pad;
	int notex = (texture_index == RENDER_NO_TEXTURE);

	(void)pad;

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

	perspdiv(&vs[0], w0);
	perspdiv(&vs[1], w1);
	perspdiv(&vs[2], w2);

//	render_set_blend_mode(RENDER_BLEND_NORMAL);
	if ((vs[0].argb & 0xff000000) < 0xff000000) {
		if ((vs[0].argb & 0xff000000) != 0x60000000)
			render_set_blend_mode(RENDER_BLEND_BRIGHTBRIGHT);
		else
			render_set_blend_mode(RENDER_BLEND_NORMAL);
	} else {
		render_set_blend_mode(RENDER_BLEND_NORMAL);
	}
	if (render_state.last_index != texture_index || render_state.cur_mode != last_mode[texture_index]) {
		// ^-- both of these need to be checked at top level, not one with one nested
		render_state.last_index = texture_index;

		last_mode[texture_index] = render_state.cur_mode;

		update_header(texture_index);

		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));
	} else if (__builtin_expect(render_state.cull_dirty,0)) {
		if(__builtin_expect(notex,0))
			pvr_prim(&chdr_notex[render_state.cull_en], sizeof(pvr_poly_hdr_t));
		else
			pvr_prim(chdr[texture_index][render_state.cull_en], sizeof(pvr_poly_hdr_t));

		render_state.cull_dirty = 0;
	}

	pvr_prim(vs, 96);
}

void  __attribute__((noinline)) render_push_sprite(vec3_t pos, vec2i_t size, uint32_t lcol, uint16_t texture_index) {
	screen_2d_z += 0.0005f;

/* 	fast_mat_store(&storemat.cols);
	fast_mat_load(&rot_sprite_mat.cols); */

	// this ordering fixes the drawing of sprites without disabling culling
	vec3_t t1 = vec3_transform(vec3( size.x * 0.5f, -size.y * 0.5f, screen_2d_z), /* &rot_ */&sprite_mat);
	vec3_t t2 = vec3_transform(vec3(-size.x * 0.5f, -size.y * 0.5f, screen_2d_z), /* &rot_ */&sprite_mat);
	vec3_t t3 = vec3_transform(vec3( size.x * 0.5f,  size.y * 0.5f, screen_2d_z), /* &rot_ */&sprite_mat);
	vec3_t t4 = vec3_transform(vec3(-size.x * 0.5f,  size.y * 0.5f, screen_2d_z), /* &rot_ */&sprite_mat);
	vec3_t p1 = vec3_add(pos, t1);
	vec3_t p2 = vec3_add(pos, t2);
	vec3_t p3 = vec3_add(pos, t3);
	vec3_t p4 = vec3_add(pos, t4);

/* 	fast_mat_load(&storemat.cols); */

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
	vs[1].u = (t->size.x  - 1.0f) * rpw;
	vs[1].v = rph;
	vs[1].argb = lcol;
//	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;
	vs[2].x = p3.x;
	vs[2].y = p3.y;
	vs[2].z = p3.z;
	vs[2].u = rpw;
	vs[2].v = (t->size.y  - 1.0f) * rph;
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
	vec2i_t tsize = render_texture_padsize(texture_index);
	float rpw = approx_recip((float)tsize.x);
	float rph = approx_recip((float)tsize.y);

	screen_2d_z += 0.0005f;

	uint32_t tilecol = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b);

//	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = pos.x;
	vs[0].y = pos.y;
	vs[0].z = screen_2d_z;
	vs[0].u = (uv_offset.x) * rpw;
	vs[0].v = (uv_offset.y) * rph;
	vs[0].argb = tilecol;
	vs[0].oargb = 0;

//	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = pos.x + size.x;
	vs[1].y = pos.y;
	vs[1].z = screen_2d_z;
	vs[1].u = (uv_offset.x + uv_size.x) * rpw;
	vs[1].v = (uv_offset.y) * rph;
	vs[1].argb = tilecol;
//	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX;
	vs[2].x = pos.x;
	vs[2].y = pos.y + size.y;
	vs[2].z = screen_2d_z;
	vs[2].u = (uv_offset.x) * rpw;
	vs[2].v = (uv_offset.y + uv_size.y) * rph;
	vs[2].argb = tilecol;
//	vs[2].oargb = 0;

	vs[3].flags = PVR_CMD_VERTEX_EOL;
	vs[3].x = pos.x + size.x;
	vs[3].y = pos.y + size.y;
	vs[3].z = screen_2d_z;
	vs[3].u = (uv_offset.x + uv_size.x) * rpw;
	vs[3].v = (uv_offset.y + uv_size.y) * rph;
	vs[3].argb = tilecol;
//	vs[3].oargb = 0;

	render_quad(texture_index);
}

#include "platform.h"

extern int is_sky;

uint16_t render_texture_create(uint32_t tw, uint32_t th, uint16_t *pixels) {
	uint16_t texture_index = textures_len++;

	if (texture_index != 0) {
		int wp2 = np2(tw);
		int hp2 = np2(th);

		if (tw == 0 || th == 0) {
			// this is the ring and something else possibly on the stopwatch model
			// 0x1 texture
			goto createbail;
		} else {
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
				// u,v issue of some kind
				// i dont really know what is going on there, but this is a color that doesnt look ridiculous
				// for any of those contexts
				for (int i = 0; i < wp2 * hp2; i++) {
					if (texture_index == 0x21)
						tmpstore[i] = 0xBDEF;
						else
							tmpstore[i] = 0;
				}

				ptrs[texture_index] = alloc_malloc(NULL, wp2 * hp2 * 2);
				// if we ran out of VRAM (we don't, but if we did), handle it by just using color poly header
				if (ptrs[texture_index == 0])
					goto createbail;

				textures[texture_index] = (render_texture_t){ {wp2, hp2}, {tw, th} };

				if (!is_sky)  {
					// copy source texture into larger pow2-padded texture
					for (uint32_t y = 0; y < th; y++)
						for(uint32_t x = 0; x < tw; x++)
							tmpstore[(y*wp2) + x] = pixels[(y*tw) + x];
					// pad horizontal space with repeated texture columns
					for (uint32_t y = 0; y < th; y++)
						for (uint32_t x = tw; x < wp2; x++)
							tmpstore[(y*wp2) + x] = tmpstore[(y*wp2) + (x-tw)];
					// pad vertical space with repeated texture rows
					for (uint32_t y = th; y < hp2; y++)
						for (uint32_t x = 0; x < wp2; x++)
							tmpstore[(y*wp2) + x] = tmpstore[((y-th)*wp2) + x];
				} else {
					// copy source texture into larger pow2-padded texture
					for (uint32_t y = 0; y < th; y++)
						for(uint32_t x = 0; x < tw; x++)
							tmpstore[(y*wp2) + x] = pixels[(y*tw) + x];

					// move some columns over and copy some
					// this mostly gets filtered skies to match unfiltered
					// Rapier Karbonis still has lines. those are present on PSX also
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+4] = tmpstore[(y*wp2)+3];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+3] = tmpstore[(y*wp2)+2];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+2] = tmpstore[(y*wp2)+1];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+1] = tmpstore[(y*wp2)];

					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+(tw-4)] = tmpstore[(y*wp2)+(tw-3)];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+(tw-3)] = tmpstore[(y*wp2)+(tw-2)];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+(tw-2)] = tmpstore[(y*wp2)+1];
					for (uint32_t y = 0; y < th; y++)
						tmpstore[(y*wp2)+(tw-1)] = tmpstore[(y*wp2)];
				}

				pvr_txr_load_ex(tmpstore, ptrs[texture_index], wp2, hp2, PVR_TXRLOAD_16BPP);
				mem_temp_free(tmpstore);
			}
		}
	}

	compile_header(texture_index);

createbail:
	if (ptrs[texture_index] == 0) {
		chdr[texture_index][0] = &chdr_notex[0];
		chdr[texture_index][1] = &chdr_notex[1];
	}

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

			free(chdr[curlen][0]);
			chdr[curlen][0] = NULL;
			chdr[curlen][1] = NULL;
		}

		last_mode[curlen] = 0;
		OGNOFILT[curlen] = 0;
	}

	textures_len = len;

	for (int i=1;i<len;i++) {
		if (!OGNOFILT[i]) {
			uint32_t *hp = (uint32_t *)chdr[i][0];
			uint32_t header2;
			header2 = hp[2];
			header2 = (header2 & ~PVR_TA_PM2_FILTER_MASK) | ((save.filter * 2) << PVR_TA_PM2_FILTER_SHIFT);
			hp[2] = header2;
			hp = (uint32_t *)chdr[i][1];
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
