#include "../types.h"
#include "../mem.h"
#include "../render.h"
#include "../utils.h"
#include "../platform.h"

#include "object.h"
#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "droid.h"
#include "camera.h"
#include "object.h"
#include "scene.h"
#include "hud.h"
#include "object.h"

#include "game.h"

#include <kos.h>

static float __attribute__((aligned(32))) w[4];
extern pvr_vertex_t vs[5];

// try to promote spatial locality for accessing these pointers
// reading one of the two pointers was missing the cache every time
// not dereferencing, just loading the pointer to begin with
typedef struct other_state {
	float *ws;
	pvr_vertex_t *evs;
} other_state_t;

static other_state_t vars;

Object *objects_load(char *name, texture_list_t tl) {
	vars.ws = &w[0];
	vars.evs = &vs[0];

	uint32_t __attribute__((aligned(32))) p = 0;
	uint32_t length = 0;
	uint8_t *bytes = platform_load_asset(name, &length);
	if (!bytes) {
		die("Failed to load file %s\n", name);
	}

	// because of padding, start of objectList is not actually this position
	// but this is where allocator needs to "unwind" to
	void *mark_it = mem_mark();
	(void)mark_it;

	Object *objectList;
	Object *prevObject = NULL;

	while (p < length) {
		char name[16];
		void *o = mem_bump(sizeof(Object) + 32);
		Object *object = (Object *)(((uintptr_t)o + 31) & ~31);

		if (p == 0) objectList = object;

		if (prevObject) {
			prevObject->next = object;
		}
		prevObject = object;

		for (int i = 0; i < 16; i++) {
			name[i] = get_i8(bytes, &p);
		}

		if (str_starts_with(name, "start")) {
			object->extent = OBJNAME_START;
		} else if (str_starts_with(name, "redl")) {
			object->extent = OBJNAME_REDL;
		} else if (str_starts_with(name, "donkey")) {
			object->extent = OBJNAME_DONKEY;
		} else if (str_starts_with(name, "lostad")) {
			object->extent = OBJNAME_LOSTAD;
		} else if (str_starts_with(name, "stad_")) {
			object->extent = OBJNAME_STAD_;
		} else if (str_starts_with(name, "newstad_")) {
			object->extent = OBJNAME_NEWSTAD_;
		} else {
			object->extent = OBJNAME_ERROR;
		}
		void *om = mem_bump(sizeof(mat4_t) + 32);
		object->mat = (mat4_t *)(((uintptr_t)om + 31) & ~31);
		object->mat->cols[0][0] = object->mat->cols[1][1] = object->mat->cols[2][2] = object->mat->cols[3][3] = 1.0f;

		object->vertices_len = get_i16(bytes, &p); p += 2;
		object->vertices = NULL; get_i32(bytes, &p);
		object->normals_len = get_i16(bytes, &p); p += 2;
		object->normals = NULL; get_i32(bytes, &p);
		object->primitives_len = get_i16(bytes, &p); p += 2;
		object->primitives = NULL; get_i32(bytes, &p);
		get_i32(bytes, &p);
		get_i32(bytes, &p);
		get_i32(bytes, &p); // Skeleton ref
		/* object->extent = */ get_i32(bytes, &p);
		object->flags = get_i16(bytes, &p); p += 2;
		object->next = NULL; get_i32(bytes, &p);

		p += 3 * 3 * 2; // relative rot matrix
		p += 2; // padding

		object->origin.x = get_i32(bytes, &p);
		object->origin.y = get_i32(bytes, &p);
		object->origin.z = get_i32(bytes, &p);

		p += 3 * 3 * 2; // absolute rot matrix
		p += 2; // padding
		p += 3 * 4; // absolute translation matrix
		p += 2; // skeleton update flag
		p += 2; // padding
		p += 4; // skeleton super
		p += 4; // skeleton sub
		p += 4; // skeleton next

		object->radius = 0;

		void *ov = mem_bump((object->vertices_len * sizeof(vector_t))+32);
		object->vertices = (vector_t *)(((uintptr_t)ov + 31) & ~31);

		void *ox = mem_bump((object->vertices_len * sizeof(vector_t))+32);
		object->xform = (vector_t *)(((uintptr_t)ox + 31) & ~31);

		for (int i = 0; i < object->vertices_len; i++) {
			object->vertices[i].x = get_i16(bytes, &p);
			object->vertices[i].y = get_i16(bytes, &p);
			object->vertices[i].z = get_i16(bytes, &p);
			object->vertices[i].w = 1;
			p += 2; // padding
// two different ways to get a radius
#if 1
			if (fabsf(object->vertices[i].x) > object->radius) {
				object->radius = fabsf(object->vertices[i].x);
			}
			if (fabsf(object->vertices[i].y) > object->radius) {
				object->radius = fabsf(object->vertices[i].y);
			}
			if (fabsf(object->vertices[i].z) > object->radius) {
				object->radius = fabsf(object->vertices[i].z);
			}
#else
			vec3_t ovec = vec3(object->vertices[i].x, object->vertices[i].y, object->vertices[i].z);
			float r0 = vec3_len(ovec);//object->vertices[i]);
			if (r0 > object->radius) object->radius = r0;
#endif
		}

		object->normals = mem_bump(object->normals_len * sizeof(vec3_t));
		for (int i = 0; i < object->normals_len; i++) {
			object->normals[i].x = get_i16(bytes, &p);
			object->normals[i].y = get_i16(bytes, &p);
			object->normals[i].z = get_i16(bytes, &p);
			p += 2; // padding
		}
		float pw,ph;
		vec2i_t tsize;

		// end of last memory bump, used to rewind to beginning of primitives
		uint32_t p_at_first_prim = p;
		object->primitives = mem_mark();

		// skipping the engine exhaust this time
		for (int i = 0; i < object->primitives_len; i++) {
			Prm prm;
			int16_t prm_type = get_i16(bytes, &p);
			int16_t prm_flag = get_i16(bytes, &p);

			switch (prm_type) {
			case PRM_TYPE_F3:
				prm.ptr = mem_bump(sizeof(F3));
				prm.f3->type = prm_type;
				prm.f3->flag = prm_flag;
				prm.f3->coords[0] = get_i16(bytes, &p);
				prm.f3->coords[1] = get_i16(bytes, &p);
				prm.f3->coords[2] = get_i16(bytes, &p);
				prm.f3->pad1 = get_i16(bytes, &p);
				// untextured polys use normal r,g,b range of 0-255
				// where 255 is full bright, unlike textured polys
				prm.f3->color = notex_argb_from_u32(get_u32(bytes, &p));
				break;

			case PRM_TYPE_F4:
				prm.ptr = mem_bump(sizeof(F4));
				prm.f4->type = prm_type;
				prm.f4->flag = prm_flag;
				prm.f4->coords[0] = get_i16(bytes, &p);
				prm.f4->coords[1] = get_i16(bytes, &p);
				prm.f4->coords[2] = get_i16(bytes, &p);
				prm.f4->coords[3] = get_i16(bytes, &p);
				// untextured polys use normal r,g,b range of 0-255
				// where 255 is full bright, unlike textured polys
				prm.f4->color = notex_argb_from_u32(get_u32(bytes, &p));
				break;

			case PRM_TYPE_FT3:
				if (flags_is(prm_flag, PRM_SHIP_ENGINE)) {
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);

					get_u16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i16(bytes, &p);
					get_u32(bytes, &p);
				} else {
					prm.ptr = mem_bump(sizeof(FT3));
					prm.ft3->type = prm_type;
					prm.ft3->flag = prm_flag;
					prm.ft3->coords[0] = get_i16(bytes, &p);
					prm.ft3->coords[1] = get_i16(bytes, &p);
					prm.ft3->coords[2] = get_i16(bytes, &p);

					prm.ft3->texture = texture_from_list(tl, get_u16(bytes, &p));
					prm.ft3->cba = get_i16(bytes, &p);
					prm.ft3->tsb = get_i16(bytes, &p);
					prm.ft3->u0 = get_i8(bytes, &p);
					prm.ft3->v0 = get_i8(bytes, &p);
					prm.ft3->u1 = get_i8(bytes, &p);
					prm.ft3->v1 = get_i8(bytes, &p);
					prm.ft3->u2 = get_i8(bytes, &p);
					prm.ft3->v2 = get_i8(bytes, &p);

					prm.ft3->pad1 = 0;
					get_i16(bytes, &p); // was pad1
					prm.ft3->color = argb_from_u32(get_u32(bytes, &p));

					tsize = render_texture_padsize(prm.ft3->texture);
					pw = 1.0f / (float)tsize.x;
					ph = 1.0f / (float)tsize.y;
					prm.ft3->u[0] = (float)prm.ft3->u0 * pw;
					prm.ft3->v[0] = (float)prm.ft3->v0 * ph;
					prm.ft3->u[1] = (float)prm.ft3->u1 * pw;
					prm.ft3->v[1] = (float)prm.ft3->v1 * ph;
					prm.ft3->u[2] = (float)prm.ft3->u2 * pw;
					prm.ft3->v[2] = (float)prm.ft3->v2 * ph;
				}
				break;

			case PRM_TYPE_FT4:
				prm.ptr = mem_bump(sizeof(FT4));
				prm.ft4->type = prm_type;
				prm.ft4->flag = prm_flag;
				prm.ft4->coords[0] = get_i16(bytes, &p);
				prm.ft4->coords[1] = get_i16(bytes, &p);
				prm.ft4->coords[2] = get_i16(bytes, &p);
				prm.ft4->coords[3] = get_i16(bytes, &p);

				prm.ft4->texture = texture_from_list(tl, get_u16(bytes, &p));
				prm.ft4->cba = get_i16(bytes, &p);
				prm.ft4->tsb = get_i16(bytes, &p);
				prm.ft4->u0 = get_i8(bytes, &p);
				prm.ft4->v0 = get_i8(bytes, &p);
				prm.ft4->u1 = get_i8(bytes, &p);
				prm.ft4->v1 = get_i8(bytes, &p);
				prm.ft4->u2 = get_i8(bytes, &p);
				prm.ft4->v2 = get_i8(bytes, &p);
				prm.ft4->u3 = get_i8(bytes, &p);
				prm.ft4->v3 = get_i8(bytes, &p);
				prm.ft4->pad1 = get_i16(bytes, &p);
				prm.ft4->color = argb_from_u32(get_u32(bytes, &p));

				tsize = render_texture_padsize(prm.ft4->texture);
				pw = 1.0f / (float)tsize.x;
				ph = 1.0f / (float)tsize.y;
				prm.ft4->u[0] = (float)prm.ft4->u0 * pw;
				prm.ft4->v[0] = (float)prm.ft4->v0 * ph;
				prm.ft4->u[1] = (float)prm.ft4->u1 * pw;
				prm.ft4->v[1] = (float)prm.ft4->v1 * ph;
				prm.ft4->u[2] = (float)prm.ft4->u2 * pw;
				prm.ft4->v[2] = (float)prm.ft4->v2 * ph;
				prm.ft4->u[3] = (float)prm.ft4->u3 * pw;
				prm.ft4->v[3] = (float)prm.ft4->v3 * ph;
				break;

			case PRM_TYPE_G3:
				prm.ptr = mem_bump(sizeof(G3));
				prm.g3->type = prm_type;
				prm.g3->flag = prm_flag;
				prm.g3->coords[0] = get_i16(bytes, &p);
				prm.g3->coords[1] = get_i16(bytes, &p);
				prm.g3->coords[2] = get_i16(bytes, &p);
				prm.g3->pad1 = get_i16(bytes, &p);
				// untextured polys use normal r,g,b range of 0-255
				// where 255 is full bright, unlike textured polys
				prm.g3->color[0] = notex_argb_from_u32(get_u32(bytes, &p));
				prm.g3->color[1] = notex_argb_from_u32(get_u32(bytes, &p));
				prm.g3->color[2] = notex_argb_from_u32(get_u32(bytes, &p));
				break;

			case PRM_TYPE_G4:
				prm.ptr = mem_bump(sizeof(G4));
				prm.g4->type = prm_type;
				prm.g4->flag = prm_flag;
				prm.g4->coords[0] = get_i16(bytes, &p);
				prm.g4->coords[1] = get_i16(bytes, &p);
				prm.g4->coords[2] = get_i16(bytes, &p);
				prm.g4->coords[3] = get_i16(bytes, &p);
				// untextured polys use normal r,g,b range of 0-255
				// where 255 is full bright, unlike textured polys
				prm.g4->color[0] = notex_argb_from_u32(get_u32(bytes, &p));
				prm.g4->color[1] = notex_argb_from_u32(get_u32(bytes, &p));
				prm.g4->color[2] = notex_argb_from_u32(get_u32(bytes, &p));
				prm.g4->color[3] = notex_argb_from_u32(get_u32(bytes, &p));
				break;

			case PRM_TYPE_GT3:
				if (flags_is(prm_flag, PRM_SHIP_ENGINE)) {
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);

					get_u16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i16(bytes, &p);
					get_u32(bytes, &p);
					get_u32(bytes, &p);
					get_u32(bytes, &p);
				} else {
					prm.ptr = mem_bump(sizeof(GT3));
					prm.gt3->type = prm_type;
					prm.gt3->flag = prm_flag;
					prm.gt3->coords[0] = get_i16(bytes, &p);
					prm.gt3->coords[1] = get_i16(bytes, &p);
					prm.gt3->coords[2] = get_i16(bytes, &p);

					prm.gt3->texture = texture_from_list(tl, get_u16(bytes, &p));
					prm.gt3->cba = get_i16(bytes, &p);
					prm.gt3->tsb = get_i16(bytes, &p);
					prm.gt3->u0 = get_i8(bytes, &p);
					prm.gt3->v0 = get_i8(bytes, &p);
					prm.gt3->u1 = get_i8(bytes, &p);
					prm.gt3->v1 = get_i8(bytes, &p);
					prm.gt3->u2 = get_i8(bytes, &p);
					prm.gt3->v2 = get_i8(bytes, &p);
					prm.gt3->pad1 = 0;
					get_i16(bytes, &p); // was pad1
					prm.gt3->color[0] = argb_from_u32(get_u32(bytes, &p));
					prm.gt3->color[1] = argb_from_u32(get_u32(bytes, &p));
					prm.gt3->color[2] = argb_from_u32(get_u32(bytes, &p));

					tsize = render_texture_padsize(prm.gt3->texture);
					pw = 1.0f / (float)tsize.x;
					ph = 1.0f / (float)tsize.y;
					prm.gt3->u[0] = (float)prm.gt3->u0 * pw;
					prm.gt3->v[0] = (float)prm.gt3->v0 * ph;
					prm.gt3->u[1] = (float)prm.gt3->u1 * pw;
					prm.gt3->v[1] = (float)prm.gt3->v1 * ph;
					prm.gt3->u[2] = (float)prm.gt3->u2 * pw;
					prm.gt3->v[2] = (float)prm.gt3->v2 * ph;
				}
				break;

			case PRM_TYPE_GT4:
				prm.ptr = mem_bump(sizeof(GT4));
				prm.gt4->type = prm_type;
				prm.gt4->flag = prm_flag;
				prm.gt4->coords[0] = get_i16(bytes, &p);
				prm.gt4->coords[1] = get_i16(bytes, &p);
				prm.gt4->coords[2] = get_i16(bytes, &p);
				prm.gt4->coords[3] = get_i16(bytes, &p);

				prm.gt4->texture = texture_from_list(tl, get_u16(bytes, &p));
				prm.gt4->cba = get_i16(bytes, &p);
				prm.gt4->tsb = get_i16(bytes, &p);
				prm.gt4->u0 = get_i8(bytes, &p);
				prm.gt4->v0 = get_i8(bytes, &p);
				prm.gt4->u1 = get_i8(bytes, &p);
				prm.gt4->v1 = get_i8(bytes, &p);
				prm.gt4->u2 = get_i8(bytes, &p);
				prm.gt4->v2 = get_i8(bytes, &p);
				prm.gt4->u3 = get_i8(bytes, &p);
				prm.gt4->v3 = get_i8(bytes, &p);
				prm.gt4->pad1 = get_i16(bytes, &p);
				prm.gt4->color[0] = argb_from_u32(get_u32(bytes, &p));
				prm.gt4->color[1] = argb_from_u32(get_u32(bytes, &p));
				prm.gt4->color[2] = argb_from_u32(get_u32(bytes, &p));
				prm.gt4->color[3] = argb_from_u32(get_u32(bytes, &p));

				tsize = render_texture_padsize(prm.gt4->texture);
				pw = 1.0f / (float)tsize.x;
				ph = 1.0f / (float)tsize.y;
				prm.gt4->u[0] = (float)prm.gt4->u0 * pw;
				prm.gt4->v[0] = (float)prm.gt4->v0 * ph;
				prm.gt4->u[1] = (float)prm.gt4->u1 * pw;
				prm.gt4->v[1] = (float)prm.gt4->v1 * ph;
				prm.gt4->u[2] = (float)prm.gt4->u2 * pw;
				prm.gt4->v[2] = (float)prm.gt4->v2 * ph;
				prm.gt4->u[3] = (float)prm.gt4->u3 * pw;
				prm.gt4->v[3] = (float)prm.gt4->v3 * ph;
				break;

			case PRM_TYPE_TSPR:
			case PRM_TYPE_BSPR:
				prm.ptr = mem_bump(sizeof(SPR));
				prm.spr->type = prm_type;
				prm.spr->flag = prm_flag;
				prm.spr->coord = get_i16(bytes, &p);
				prm.spr->width = get_i16(bytes, &p);
				prm.spr->height = get_i16(bytes, &p);
				prm.spr->texture = texture_from_list(tl, get_i16(bytes, &p));
				prm.spr->color = argb_from_u32(get_u32(bytes, &p));
				break;

			default:
				die("bad primitive type %x \n", prm_type);
			} // switch
		} // each prim

		// adding just the engine exhaust this time
		p = p_at_first_prim;
		for (int i = 0; i < object->primitives_len; i++) {
			Prm prm;
			int16_t prm_type = get_i16(bytes, &p);
			int16_t prm_flag = get_i16(bytes, &p);
			switch (prm_type) {
			case PRM_TYPE_F3:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_F4:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_FT3:
				if (flags_is(prm_flag, PRM_SHIP_ENGINE)) {
					prm.ptr = mem_bump(sizeof(FT3));
					prm.ft3->type = prm_type;
					prm.ft3->flag = prm_flag;
					prm.ft3->coords[0] = get_i16(bytes, &p);
					prm.ft3->coords[1] = get_i16(bytes, &p);
					prm.ft3->coords[2] = get_i16(bytes, &p);

					prm.ft3->texture = texture_from_list(tl, get_u16(bytes, &p));
					prm.ft3->cba = get_i16(bytes, &p);
					prm.ft3->tsb = get_i16(bytes, &p);
					prm.ft3->u0 = get_i8(bytes, &p);
					prm.ft3->v0 = get_i8(bytes, &p);
					prm.ft3->u1 = get_i8(bytes, &p);
					prm.ft3->v1 = get_i8(bytes, &p);
					prm.ft3->u2 = get_i8(bytes, &p);
					prm.ft3->v2 = get_i8(bytes, &p);

					prm.ft3->pad1 = 0;
					get_i16(bytes, &p); // was pad1
					// engine colors are 25% of what they should be
					prm.ft3->color = eng_argb_from_u32(get_u32(bytes, &p));

					tsize = render_texture_padsize(prm.ft3->texture);
					pw = 1.0f / (float)tsize.x;
					ph = 1.0f / (float)tsize.y;
					prm.ft3->u[0] = (float)prm.ft3->u0 * pw;
					prm.ft3->v[0] = (float)prm.ft3->v0 * ph;
					prm.ft3->u[1] = (float)prm.ft3->u1 * pw;
					prm.ft3->v[1] = (float)prm.ft3->v1 * ph;
					prm.ft3->u[2] = (float)prm.ft3->u2 * pw;
					prm.ft3->v[2] = (float)prm.ft3->v2 * ph;
				} else {
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);

					get_u16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i16(bytes, &p);
					get_u32(bytes, &p);
				}
				break;

			case PRM_TYPE_FT4:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);

				get_u16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_G3:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_G4:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_GT3:
				if (flags_is(prm_flag, PRM_SHIP_ENGINE)) {
					prm.ptr = mem_bump(sizeof(GT3));
					prm.gt3->type = prm_type;
					prm.gt3->flag = prm_flag;
					prm.gt3->coords[0] = get_i16(bytes, &p);
					prm.gt3->coords[1] = get_i16(bytes, &p);
					prm.gt3->coords[2] = get_i16(bytes, &p);

					prm.gt3->texture = texture_from_list(tl, get_u16(bytes, &p));
					prm.gt3->cba = get_i16(bytes, &p);
					prm.gt3->tsb = get_i16(bytes, &p);
					prm.gt3->u0 = get_i8(bytes, &p);
					prm.gt3->v0 = get_i8(bytes, &p);
					prm.gt3->u1 = get_i8(bytes, &p);
					prm.gt3->v1 = get_i8(bytes, &p);
					prm.gt3->u2 = get_i8(bytes, &p);
					prm.gt3->v2 = get_i8(bytes, &p);
					prm.gt3->pad1 = 0;
					get_i16(bytes, &p); // was pad1
					// engine colors are 25% of what they should be
					prm.gt3->color[0] = eng_argb_from_u32(get_u32(bytes, &p));
					prm.gt3->color[1] = eng_argb_from_u32(get_u32(bytes, &p));
					prm.gt3->color[2] = eng_argb_from_u32(get_u32(bytes, &p));

					tsize = render_texture_padsize(prm.gt3->texture);
					pw = 1.0f / (float)tsize.x;
					ph = 1.0f / (float)tsize.y;
					prm.gt3->u[0] = (float)prm.gt3->u0 * pw;
					prm.gt3->v[0] = (float)prm.gt3->v0 * ph;
					prm.gt3->u[1] = (float)prm.gt3->u1 * pw;
					prm.gt3->v[1] = (float)prm.gt3->v1 * ph;
					prm.gt3->u[2] = (float)prm.gt3->u2 * pw;
					prm.gt3->v[2] = (float)prm.gt3->v2 * ph;
				} else {
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);

					get_u16(bytes, &p);
					get_i16(bytes, &p);
					get_i16(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i8(bytes, &p);
					get_i16(bytes, &p);
					get_u32(bytes, &p);
					get_u32(bytes, &p);
					get_u32(bytes, &p);
				}
				break;

			case PRM_TYPE_GT4:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);

				get_u16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i8(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				get_u32(bytes, &p);
				break;

			case PRM_TYPE_TSPR:
			case PRM_TYPE_BSPR:
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_i16(bytes, &p);
				get_u32(bytes, &p);
				break;

			default:
				die("bad primitive type %x \n", prm_type);
			} // switch
		} // each prim

	} // each object

	mem_temp_free(bytes);
	return objectList;
}





struct SortedSprite_s {
	uint8_t *ptr;
	vector_t *vertex;
	mat4_t *mat;
	uint32_t pad;
};

struct SortedSprite_s __attribute__((aligned(32))) sprs[256];

int sprites_to_draw = 0;
int max_sprites_to_draw = 0;
void draw_all_sprites(void) {
	int mat_change = 0;
	mat4_t *last_mat = NULL;

	for (int i=0;i<sprites_to_draw;i++) {
		struct SortedSprite_s *tmp = &sprs[i];
		Prm poly = {.primitive = (Primitive*)tmp->ptr};
		int coord0 = poly.spr->coord;

		// don't need to change the model matrix for every sprite drawn, there is a savings here
		if (last_mat != tmp->mat) {
			render_set_model_mat(tmp->mat);
			last_mat = tmp->mat;
			mat_change++;
		}

		render_push_sprite(
			vec3(
				tmp->vertex[coord0].x,
				tmp->vertex[coord0].y + ((poly.primitive->type == PRM_TYPE_TSPR ? poly.spr->height : -poly.spr->height) >> 1),
				tmp->vertex[coord0].z
			),
			vec2i(poly.spr->width, poly.spr->height),
			poly.spr->color,
			poly.spr->texture
		);
	}

	sprites_to_draw = 0;
}

void emplace_ssp(uint8_t *p, mat4_t *mat, vector_t *v) {
	if (sprites_to_draw == 256) return;
	struct SortedSprite_s *rsp = &sprs[sprites_to_draw++];
	rsp->ptr = p;
	rsp->mat = mat;
	rsp->vertex = v;
}

static void (*quadfunc)(uint16_t texture_index, float *w);
static void (*trifunc)(uint16_t texture_index, float *w);

int xform_all_verts(vector_t *output, vector_t *input, int c);

void object_draw(Object *object, mat4_t *mat) {
	vector_t *vertex = object->xform;
	Prm poly = {.primitive = object->primitives};
	int primitives_len = object->primitives_len;

	// scope-local redefinitions
	float *w = vars.ws;
	pvr_vertex_t *vs = vars.evs;

	render_set_model_mat(mat);
	int wtest = xform_all_verts(object->xform, object->vertices, object->vertices_len);
	if (wtest) {
		quadfunc = render_quad_noxform_noclip;
		trifunc = render_tri_noxform_noclip;
	} else {
		quadfunc = render_quad_noxform;
		trifunc = render_tri_noxform;
	}

	for (int i = 0; i < primitives_len; i++) {
		int coord0;
		int coord1;
		int coord2;
		int coord3;
		uint32_t argb;
		int16_t type = poly.primitive->type;

		if (type && type < 9) {
			if (flags_is(poly.primitive->flag, PRM_SINGLE_SIDED)) {
				render_set_cull_backface(true);
			} else {
				render_set_cull_backface(false);
			}
		} else {
			render_set_cull_backface(true);
		}

		switch (type) {
		case PRM_TYPE_GT3:
			coord0 = poly.gt3->coords[0];
			coord1 = poly.gt3->coords[1];
			coord2 = poly.gt3->coords[2];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].u = poly.gt3->u[0];
			vs[0].v = poly.gt3->v[0];
			vs[0].argb = poly.gt3->color[0];
			vs[0].oargb = (poly.gt3->pad1 == 1000 ? poly.gt3->color[0] : 1);

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].u = poly.gt3->u[1];
			vs[1].v = poly.gt3->v[1];
			vs[1].argb = poly.gt3->color[1];
			//vs[1].oargb = (poly.gt3->pad1 ? poly.gt3->color[1] : 1);

			vs[2].flags = PVR_CMD_VERTEX_EOL;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].u = poly.gt3->u[2];
			vs[2].v = poly.gt3->v[2];
			vs[2].argb = poly.gt3->color[2];
			//vs[2].oargb = (poly.gt3->pad1 ? poly.gt3->color[2] : 1);

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;

			trifunc(poly.gt3->texture, w);
			poly.gt3 += 1;
			break;

		case PRM_TYPE_GT4:
			coord0 = poly.gt4->coords[0];
			coord1 = poly.gt4->coords[1];
			coord2 = poly.gt4->coords[2];
			coord3 = poly.gt4->coords[3];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].u = poly.gt4->u[0];
			vs[0].v = poly.gt4->v[0];
			vs[0].argb = poly.gt4->color[0];
			vs[0].oargb = 1;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].u = poly.gt4->u[1];
			vs[1].v = poly.gt4->v[1];
			vs[1].argb = poly.gt4->color[1];
			//vs[1].oargb = 1;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].u = poly.gt4->u[2];
			vs[2].v = poly.gt4->v[2];
			vs[2].argb = poly.gt4->color[2];
			//vs[2].oargb = 1;

			vs[3].flags = PVR_CMD_VERTEX_EOL;
			vs[3].x = vertex[coord3].x;
			vs[3].y = vertex[coord3].y;
			vs[3].z = vertex[coord3].z;
			vs[3].u = poly.gt4->u[3];
			vs[3].v = poly.gt4->v[3];
			vs[3].argb = poly.gt4->color[3];
			//vs[3].oargb = 1;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;
			w[3] = object->xform[coord3].w;

			quadfunc(poly.gt4->texture, w);

			poly.gt4 += 1;
			break;

		case PRM_TYPE_FT3:
			argb = poly.ft3->color;

			coord0 = poly.ft3->coords[0];
			coord1 = poly.ft3->coords[1];
			coord2 = poly.ft3->coords[2];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].u = poly.ft3->u[0];
			vs[0].v = poly.ft3->v[0];
			vs[0].argb = argb;
			vs[0].oargb = (poly.ft3->pad1 == 1000 ? poly.ft3->color : 0);

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].u = poly.ft3->u[1];
			vs[1].v = poly.ft3->v[1];
			vs[1].argb = argb;
			//vs[1].oargb = oargb;

			vs[2].flags = PVR_CMD_VERTEX_EOL;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].u = poly.ft3->u[2];
			vs[2].v = poly.ft3->v[2];
			vs[2].argb = argb;
			//vs[2].oargb = oargb;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;

			trifunc(poly.ft3->texture, w);

			poly.ft3 += 1;
			break;

		case PRM_TYPE_FT4:
			argb = poly.ft4->color;

			coord0 = poly.ft4->coords[0];
			coord1 = poly.ft4->coords[1];
			coord2 = poly.ft4->coords[2];
			coord3 = poly.ft4->coords[3];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].u = poly.ft4->u[0];
			vs[0].v = poly.ft4->v[0];
			vs[0].argb = argb;
			vs[0].oargb = 0;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].u = poly.ft4->u[1];
			vs[1].v = poly.ft4->v[1];
			vs[1].argb = argb;
			//vs[1].oargb = 0;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].u = poly.ft4->u[2];
			vs[2].v = poly.ft4->v[2];
			vs[2].argb = argb;
			//vs[2].oargb = 0;

			vs[3].flags = PVR_CMD_VERTEX_EOL;
			vs[3].x = vertex[coord3].x;
			vs[3].y = vertex[coord3].y;
			vs[3].z = vertex[coord3].z;
			vs[3].u = poly.ft4->u[3];
			vs[3].v = poly.ft4->v[3];
			vs[3].argb = argb;
			//vs[3].oargb = 0;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;
			w[3] = object->xform[coord3].w;

			quadfunc(poly.ft4->texture, w);

			poly.ft4 += 1;
			break;

		case PRM_TYPE_G3:
			coord0 = poly.g3->coords[0];
			coord1 = poly.g3->coords[1];
			coord2 = poly.g3->coords[2];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].argb = poly.g3->color[0];
			vs[0].oargb = 1;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].argb = poly.g3->color[1];
			//vs[1].oargb = 1;

			vs[2].flags = PVR_CMD_VERTEX_EOL;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].argb = poly.g3->color[2];
			//vs[2].oargb = 1;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;

			trifunc(RENDER_NO_TEXTURE, w);

			poly.g3 += 1;
			break;

		case PRM_TYPE_G4:
			coord0 = poly.g4->coords[0];
			coord1 = poly.g4->coords[1];
			coord2 = poly.g4->coords[2];
			coord3 = poly.g4->coords[3];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].argb = poly.g4->color[0];
			vs[0].oargb = 1;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].argb = poly.g4->color[1];
			//vs[1].oargb = 1;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].argb = poly.g4->color[2];
			//vs[2].oargb = 1;

			vs[3].flags = PVR_CMD_VERTEX_EOL;
			vs[3].x = vertex[coord3].x;
			vs[3].y = vertex[coord3].y;
			vs[3].z = vertex[coord3].z;
			vs[3].argb = poly.g4->color[3];
			//vs[3].oargb = 1;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;
			w[3] = object->xform[coord3].w;

			quadfunc(RENDER_NO_TEXTURE, w);

			poly.g4 += 1;
			break;

		case PRM_TYPE_F3:
			argb = poly.f3->color;
			coord0 = poly.f3->coords[0];
			coord1 = poly.f3->coords[1];
			coord2 = poly.f3->coords[2];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].argb = argb;
			vs[0].oargb = 0;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].argb = argb;
			//vs[1].oargb = 0;

			vs[2].flags = PVR_CMD_VERTEX_EOL;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].argb = argb;
			//vs[2].oargb = 0;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;

			trifunc(RENDER_NO_TEXTURE, w);

			poly.f3 += 1;
			break;

		case PRM_TYPE_F4:
			argb = poly.f4->color;
			coord0 = poly.f4->coords[0];
			coord1 = poly.f4->coords[1];
			coord2 = poly.f4->coords[2];
			coord3 = poly.f4->coords[3];

			//vs[0].flags = PVR_CMD_VERTEX;
			vs[0].x = vertex[coord0].x;
			vs[0].y = vertex[coord0].y;
			vs[0].z = vertex[coord0].z;
			vs[0].argb = argb;
			vs[0].oargb = 0;

			//vs[1].flags = PVR_CMD_VERTEX;
			vs[1].x = vertex[coord1].x;
			vs[1].y = vertex[coord1].y;
			vs[1].z = vertex[coord1].z;
			vs[1].argb = argb;
			//vs[1].oargb = 0;

			vs[2].flags = PVR_CMD_VERTEX;
			vs[2].x = vertex[coord2].x;
			vs[2].y = vertex[coord2].y;
			vs[2].z = vertex[coord2].z;
			vs[2].argb = argb;
			//vs[2].oargb = 0;

			vs[3].flags = PVR_CMD_VERTEX_EOL;
			vs[3].x = vertex[coord3].x;
			vs[3].y = vertex[coord3].y;
			vs[3].z = vertex[coord3].z;
			vs[3].argb = argb;
			//vs[3].oargb = 0;

			w[0] = object->xform[coord0].w;
			w[1] = object->xform[coord1].w;
			w[2] = object->xform[coord2].w;
			w[3] = object->xform[coord3].w;

			quadfunc(RENDER_NO_TEXTURE, w);

			poly.f4 += 1;
			break;

		case PRM_TYPE_TSPR:
		case PRM_TYPE_BSPR:
			emplace_ssp(poly.ptr, mat, object->vertices);
			poly.spr += 1;
			break;

		default:
			break;
		}
	}
}
