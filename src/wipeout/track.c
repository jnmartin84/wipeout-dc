#include "../mem.h"
#include "../utils.h"
#include "../render.h"
#include "../system.h"
#include "../platform.h"

#include "object.h"
#include "track.h"
#include "camera.h"
#include "object.h"
#include "game.h"

void track_load(const char *base_path) {
	// Load and assemble high res track tiles

	g.track.textures.start = render_textures_len();
	g.track.textures.len = 0;

	ttf_t *ttf = track_load_tile_format(get_path(base_path, "library.ttf"));
	cmp_t *cmp = image_load_compressed(get_path(base_path, "library.cmp"));

	image_t *temp_tile = image_alloc(128, 128);
	for (int i = 0; i < ttf->len; i++) {
		for (int tx = 0; tx < 4; tx++) {
			for (int ty = 0; ty < 4; ty++) {
				uint32_t sub_tile_index = ttf->tiles[i].near[ty * 4 + tx];
				image_t *sub_tile = image_load_from_bytes(cmp->entries[sub_tile_index], false);
				image_copy(sub_tile, temp_tile, 0, 0, 32, 32, tx * 32, ty * 32);
				mem_temp_free(sub_tile);
			}
		}
		render_texture_create(temp_tile->width, temp_tile->height, temp_tile->pixels);
		g.track.textures.len++;
	}

	mem_temp_free(temp_tile);
	mem_temp_free(cmp);
	mem_temp_free(ttf);

	vec3_t *vertices = track_load_vertices(get_path(base_path, "track.trv"));
	track_load_faces(get_path(base_path, "track.trf"), vertices);
	mem_temp_free(vertices);

	track_load_sections(get_path(base_path, "track.trs"));

	g.track.pickups_len = 0;
	section_t *s = g.track.sections;
	section_t *j = NULL;

	// Nummerate all sections; take care to give both stretches at a junction
	// the same numbers.
	int num = 0;
	do {
		s->num = num++;
		if (s->junction) { // start junction
			j = s->junction;
			do {
				j->num = num++;
				j = j->next;
			} while (!j->junction); // end junction
			num = s->num;
		}
		s = s->next;
	} while (s != g.track.sections);
	g.track.total_section_nums = num;

	g.track.pickups = mem_mark();
	for (int i = 0; i < g.track.section_count; i++) {
		track_face_t *face = track_section_get_base_face(&g.track.sections[i]);

		for (int f = 0; f < 2; f++) {
			if (flags_any(face->flags, FACE_PICKUP_RIGHT | FACE_PICKUP_LEFT)) {
				mem_bump(sizeof(track_pickup_t));
				g.track.pickups[g.track.pickups_len].face = face;
				g.track.pickups[g.track.pickups_len].cooldown_timer = 0;
				g.track.pickups_len++;
			}

			if (flags_is(face->flags, FACE_BOOST)) {
				track_face_set_color(face, 0, 0, 255);
			}
			face++;
		}
	}
}

ttf_t *track_load_tile_format(char *ttf_name) {
	uint32_t ttf_size;
	uint8_t *ttf_bytes = platform_load_asset(ttf_name, &ttf_size);

	uint32_t p = 0;
	uint32_t num_tiles = ttf_size / 42;

	ttf_t *ttf = mem_temp_alloc(sizeof(ttf_t) + sizeof(ttf_tile_t) * num_tiles);
	ttf->len = num_tiles;

	for (int t = 0; t < num_tiles; t++) {
		for (int i = 0; i < 16; i++) {
			ttf->tiles[t].near[i] = get_i16(ttf_bytes, &p);
		}
		for (int i = 0; i < 4; i++) {
			ttf->tiles[t].med[i] = get_i16(ttf_bytes, &p);
		}
		ttf->tiles[t].far = get_i16(ttf_bytes, &p);
	}
	mem_temp_free(ttf_bytes);

	return ttf;
}

bool track_collect_pickups(track_face_t *face) {
	if (flags_is(face->flags, FACE_PICKUP_ACTIVE)) {
		flags_rm(face->flags, FACE_PICKUP_ACTIVE);
		flags_add(face->flags, FACE_PICKUP_COLLECTED);
		track_face_set_color(face, 255, 255, 255);
		return true;
	}
	else {
		return false;
	}
}

vec3_t *track_load_vertices(char *file_name) {
	uint32_t size;
	uint8_t *bytes = platform_load_asset(file_name, &size);

	g.track.vertex_count = size / 16; // VECTOR_SIZE
	vec3_t *vertices = mem_temp_alloc(sizeof(vec3_t) * g.track.vertex_count);

	uint32_t p = 0;
	for (int i = 0; i < g.track.vertex_count; i++) {
		vertices[i].x = get_i32(bytes, &p);
		vertices[i].y = get_i32(bytes, &p);
		vertices[i].z = get_i32(bytes, &p);
		p += 4; // padding
	}

	mem_temp_free(bytes);
	return vertices;
}

static const vec2_t track_uv[2][4] = {
	{{128, 0}, {  0, 0}, {  0, 128}, {128, 128}},
	{{  0, 0}, {128, 0}, {128, 128}, {  0, 128}}
};

void track_load_faces(char *file_name, vec3_t *vertices) {
	uint32_t size;
	uint8_t *bytes = platform_load_asset(file_name, &size);

	g.track.face_count = size / 20; // TRACK_FACE_DATA_SIZE
	g.track.faces = mem_bump(sizeof(track_face_t) * g.track.face_count);
	void *trackpvr = mem_bump((sizeof(pvr_vertex_t) * 4 * g.track.face_count) + 32);
	g.track.apvr = (pvr_vertex_t *)(((uintptr_t)trackpvr + 31) & ~31);

	uint32_t p = 0;
	track_face_t *tf = g.track.faces;
	uint16_t tstart = g.track.textures.start;

	for (int i = 0; i < g.track.face_count; i++) {

		vec3_t v0 = vertices[get_i16(bytes, &p)];
		vec3_t v1 = vertices[get_i16(bytes, &p)];
		vec3_t v2 = vertices[get_i16(bytes, &p)];
		vec3_t v3 = vertices[get_i16(bytes, &p)];
		tf->normal.x = (float)get_i16(bytes, &p) / 4096.0f;
		tf->normal.y = (float)get_i16(bytes, &p) / 4096.0f;
		tf->normal.z = (float)get_i16(bytes, &p) / 4096.0f;

		tf->texture = get_i8(bytes, &p);
		tf->flags = get_i8(bytes, &p);
		vec2i_t tsize = render_texture_padsize(tstart + tf->texture);
		float pw = (float)tsize.x;
		float ph = (float)tsize.y;

		uint32_t lcol = argb_from_u32(get_u32(bytes, &p));

		const vec2_t *uv = track_uv[flags_is(tf->flags, FACE_FLIP_TEXTURE) ? 1 : 0];
		pvr_vertex_t *tf_verts = &g.track.apvr[i<<2];
		tf->vp = tf_verts;

		// these look redundant but they are used for collision
		tf_verts[0] = (pvr_vertex_t){
				.flags = PVR_CMD_VERTEX,
				.x = v3.x,
				.y = v3.y,
				.z = v3.z,
				.u = uv[3].x / pw,
				.v = uv[3].y / ph,
				.argb = lcol,
				.oargb = 0,
				};
		tf_verts[1] = (pvr_vertex_t){.flags = PVR_CMD_VERTEX,
				.x = v2.x,
				.y = v2.y,
				.z = v2.z,
				.u = uv[2].x / pw,
				.v = uv[2].y / ph,
				.argb = lcol,
				.oargb = 0,
				};
		tf_verts[2] = (pvr_vertex_t){
				.flags = PVR_CMD_VERTEX,
				.x = v0.x,
				.y = v0.y,
				.z = v0.z,
				.u = uv[0].x / pw,
				.v = uv[0].y / ph,
				.argb = lcol,
				.oargb = 0,
				},
		tf_verts[3] = (pvr_vertex_t){
				.flags = PVR_CMD_VERTEX_EOL,
				.x = v1.x,
				.y = v1.y,
				.z = v1.z,
				.u = uv[1].x / pw,
				.v = uv[1].y / ph,
				.argb = lcol,
				.oargb = 0,
				},

		tf->tris[0] = (tris_t){
			.vertices = {
				{//.pos = v0, .uv = uv[0], .color = color, .spec = 0
				.flags = 0,
				.x = v0.x,
				.y = v0.y,
				.z = v0.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
				{//.pos = v1, .uv = uv[1], .color = color, .spec = 0
				.flags = 0,
				.x = v1.x,
				.y = v1.y,
				.z = v1.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
				{//.pos = v2, .uv = uv[2], .color = color, .spec = 0
				.flags = 0,
				.x = v2.x,
				.y = v2.y,
				.z = v2.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
			}
		};
		tf->tris[1] = (tris_t){
			.vertices = {
				{//.pos = v3, .uv = uv[3], .color = color, .spec = 0
				.flags = 0,
				.x = v3.x,
				.y = v3.y,
				.z = v3.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
				{//.pos = v0, .uv = uv[0], .color = color, .spec = 0
				.flags = 0,
				.x = v0.x,
				.y = v0.y,
				.z = v0.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
				{//.pos = v2, .uv = uv[2], .color = color, .spec = 0
				.flags = 0,
				.x = v2.x,
				.y = v2.y,
				.z = v2.z,
				.u = 0,
				.v = 0,
				.argb = 0,
				.oargb = 0,
				},
			}
		};

		tf++;
	}

	mem_temp_free(bytes);
}

void track_load_sections(char *file_name) {
	uint32_t size;
	uint8_t *bytes = platform_load_asset(file_name, &size);

	g.track.section_count = size / 156; // SECTION_DATA_SIZE
	// if you need to know section count for anything
	//printf("%d\n", g.track.section_count);
	g.track.sections = mem_bump(sizeof(section_t) * g.track.section_count);

	uint32_t p = 0;
	section_t *ts = g.track.sections;
	for (int i = 0; i < g.track.section_count; i++) {
		// section-specific scenery and track surface draw distance init
#define DEFAULT_LOW_DISTSQ (float)(64000.0f * 64000.0f)
#define DEFAULT_MED_DISTSQ (float)(76000.0f * 76000.0f)
#define DEFAULT_HIGH_DISTSQ (float)(96000.0f * 96000.0f)
#define DEFAULT_XHIGH_DISTSQ (float)(120000.0f * 120000.0f)
#define DEFAULT_XXHIGH_DISTSQ (float)(256000.0f * 256000.0f)

		if (g.circut == CIRCUT_KARBONIS_V) {
			if (i >= 179 && i <= 219) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_TERRAMAX) {
			if (i >= 0 && i <= 32) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i == 318) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_KORODERA) {
			if (i >= 125 && i <= 147) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 318 && i <= 362) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 412 && i <= 436) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else if (i >= 489 && i <= 515) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_ARRIDOS_IV) {
			if (i >= 172 && i <= 179) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else if (i >= 181 && i <= 192) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 197 && i <= 220) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 256 && i <= 286) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 295 && i <= 308) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 427 && i <= 450) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_FIRESTAR) {
 			if (i >= 0 && i <= 75) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else if (i >= 96 && i <= 115) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else if (i >= 380 && i <= 386) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_SILVERSTREAM) {
			if (i == 1) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 155 && i <= 270) {
				ts->drawdist = DEFAULT_MED_DISTSQ;
			} else if (i == 0 || (i >= 335 && i <= 363)) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->drawdist = DEFAULT_HIGH_DISTSQ;
			}
		} else {
			ts->drawdist = DEFAULT_HIGH_DISTSQ;
		}

		ts->scenedist = ts->drawdist;

		if (g.circut == CIRCUT_ALTIMA_VII) {
			// i want to see whatever that weird thing that extends toward the sky
			// right before the tunnel at the end of the race
			// also right after stands at beginning
			if (i >= 38 && i <= 118) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
				ts->scenedist = DEFAULT_XHIGH_DISTSQ;
			} else if (i >= 490 && i <= 540) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
				ts->scenedist = DEFAULT_XHIGH_DISTSQ;
			} else if (i >= 541 && i <= 585) {
				ts->drawdist = DEFAULT_LOW_DISTSQ;
				ts->scenedist = DEFAULT_LOW_DISTSQ;		
			}
		} else if (g.circut == CIRCUT_ARRIDOS_IV) {
			if (i >= 172 && i <= 179) {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			} else if (i >= 181 && i <= 192) {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			} else if (i >= 197 && i <= 220) {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			} else if (i >= 256 && i <= 286) {
				ts->scenedist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 295 && i <= 308) {
				ts->scenedist = DEFAULT_LOW_DISTSQ;
			} else if (i >= 427 && i <= 450) {
				ts->scenedist = DEFAULT_LOW_DISTSQ;
			} else {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_SILVERSTREAM) {
 			if (i >= 155 && i <= 270) {
				ts->scenedist = DEFAULT_MED_DISTSQ;
			} else if (i == 0 || (i >= 335 && i <= 363)) {
				ts->scenedist = DEFAULT_MED_DISTSQ;
			} 
		} else if (g.circut == CIRCUT_TERRAMAX) {
			if (i >= 0 && i <= 32) {
				ts->scenedist = DEFAULT_XXHIGH_DISTSQ;
			} else if (i == 318) {
				ts->scenedist = DEFAULT_XXHIGH_DISTSQ;
			} else {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			}
		} else if (g.circut == CIRCUT_FIRESTAR) {
			if (i >= 0 && i <= 14) {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			} else if (i >= 380 && i <= 386) {
				ts->scenedist = DEFAULT_HIGH_DISTSQ;
			}
		}

		ts->base_face = NULL;
		int32_t junction_index = get_i32(bytes, &p);
		if (junction_index != -1) {
			ts->junction = g.track.sections + junction_index;
		}
		else {
			ts->junction = NULL;
		}

		ts->prev = g.track.sections + get_i32(bytes, &p);
		ts->next = g.track.sections + get_i32(bytes, &p);

		ts->center.x = get_i32(bytes, &p);
		ts->center.y = get_i32(bytes, &p);
		ts->center.z = get_i32(bytes, &p);

		int16_t version = get_i16(bytes, &p);
		error_if(version != TRACK_VERSION, "Convert track with track10: section: %d Track: %d\n", version, TRACK_VERSION);
		p += 2; // padding

		p += 4 + 4; // objects pointer, objectCount
		p += 5 * 3 * 4; // view section pointers
		p += 5 * 3 * 2; // view section counts

		p += 4 * 2; // high list
		p += 4 * 2; // med list

		// face start
		// face count
		// flags
		// num

		ts->face_start = get_i16(bytes, &p);
		ts->face_count = get_i16(bytes, &p);

		// JNMARTIN84: ADD BOUNDING RADIUS CALCULATION
		ts->radius = 0.0f;

		vec3_t v0,v1,v2,v3;
		for (int j=0;j<ts->face_count;j++) {
			track_face_t *tfc = &g.track.faces[ts->face_start + j];
			if (flags_any(tfc->flags, FACE_TRACK_BASE)) {
				ts->base_face = (track_face_t *)((uintptr_t)tfc - 0x100);
			}

#if 1
			v0.x = fabsf(tfc->vp[0].x - ts->center.x);
			v0.y = fabsf(tfc->vp[0].y - ts->center.y);
			v0.z = fabsf(tfc->vp[0].z - ts->center.z);

			if (v0.x > ts->radius) ts->radius = v0.x;
			if (v0.y > ts->radius) ts->radius = v0.y;
			if (v0.z > ts->radius) ts->radius = v0.z;

			v1.x = fabsf(tfc->vp[1].x - ts->center.x);
			v1.y = fabsf(tfc->vp[1].y - ts->center.y);
			v1.z = fabsf(tfc->vp[1].z - ts->center.z);

			if (v1.x > ts->radius) ts->radius = v1.x;
			if (v1.y > ts->radius) ts->radius = v1.y;
			if (v1.z > ts->radius) ts->radius = v1.z;

			v2.x = fabsf(tfc->vp[2].x - ts->center.x);
			v2.y = fabsf(tfc->vp[2].y - ts->center.y);
			v2.z = fabsf(tfc->vp[2].z - ts->center.z);

			if (v2.x > ts->radius) ts->radius = v2.x;
			if (v2.y > ts->radius) ts->radius = v2.y;
			if (v2.z > ts->radius) ts->radius = v2.z;

			v3.x = fabsf(tfc->vp[3].x - ts->center.x);
			v3.y = fabsf(tfc->vp[3].y - ts->center.y);
			v3.z = fabsf(tfc->vp[3].z - ts->center.z);

			if (v3.x > ts->radius) ts->radius = v3.x;
			if (v3.y > ts->radius) ts->radius = v3.y;
			if (v3.z > ts->radius) ts->radius = v3.z;
#else
			float r0,r1,r2,r3;
			r0 = vec3_len(vec3_sub(v0,ts->center));
			r1 = vec3_len(vec3_sub(v1,ts->center));
			r2 = vec3_len(vec3_sub(v2,ts->center));
			r3 = vec3_len(vec3_sub(v3,ts->center));
			if (r0 > ts->radius) ts->radius = r0;
			if (r1 > ts->radius) ts->radius = r1;
			if (r2 > ts->radius) ts->radius = r2;
			if (r3 > ts->radius) ts->radius = r3;
#endif
		}
		p += 2 * 2; // global/local radius

		ts->flags = get_i16(bytes, &p);
		ts->num = get_i16(bytes, &p);
		p += 2; // padding
		ts++;
	}

	mem_temp_free(bytes);
}

#include <kos.h>

extern pvr_vertex_t __attribute__((aligned(32))) vs[5];

extern void memcpy32(const void *dst, const void *src, size_t s);

void track_draw_section(section_t *section) {
	track_face_t *face = g.track.faces + section->face_start;
	int16_t face_count = section->face_count;
	int16_t tstart = g.track.textures.start;
	int16_t fc2 = section->face_count >> 1;
	int16_t leftover = face_count & 1;

	void *vp = (void *)face->vp;
 	for (int16_t j = 0; j < fc2; j++) {
		// this is about as good as it gets for setting up
		// one quad for rendering
		// memcpy32 of size 128 completes in ~180 cycles
		// around 1.4 cycles per byte
		memcpy32(vs, vp, 128);

		vp = vp + 128;

		render_quad(tstart + face++->texture);

		memcpy32(vs, vp, 128);

		vp = vp + 128;

		render_quad(tstart + face++->texture);
	}

	if (leftover) {
		memcpy32(vs, vp, 128);
		render_quad(tstart + face->texture);
	}
}

void track_draw(camera_t *camera) {
	render_set_model_ident();

	// Calculate the camera forward vector, so we can cull everything that's
	// behind. Ideally we'd want to do a full frustum culling here.
	vec3_t cam_pos = camera->position;
	vec3_t cam_dir = camera_forward(camera);

	// track draw dist for player ship current section
	float drawdist = g.track.sections[g.ships[g.pilot].section_num].drawdist;

	for(int32_t i = 0; i < g.track.section_count; i++) {
		section_t *s = &g.track.sections[i];
		vec3_t diff = vec3_sub(cam_pos, s->center);
		float cam_dot = vec3_dot(diff, cam_dir);
		float dist_sq = vec3_dot(diff, diff);

		if (cam_dot < s->radius &&
			dist_sq < drawdist
		) {
			track_draw_section(s);
		}
	}
}

void track_cycle_pickups(void) {
	float pickup_cycle_time = 1.5 * system_cycle_time();

	for (int i = 0; i < g.track.pickups_len; i++) {
		if (flags_is(g.track.pickups[i].face->flags, FACE_PICKUP_COLLECTED)) {
			flags_rm(g.track.pickups[i].face->flags, FACE_PICKUP_COLLECTED);
			g.track.pickups[i].cooldown_timer = TRACK_PICKUP_COOLDOWN_TIME;
		}
		else if (g.track.pickups[i].cooldown_timer <= 0) {
			flags_add(g.track.pickups[i].face->flags, FACE_PICKUP_ACTIVE);
			track_face_set_color(g.track.pickups[i].face,
				sinf( pickup_cycle_time + i) * 127 + 128,
				cosf( pickup_cycle_time + i) * 127 + 128,
				sinf(-pickup_cycle_time - i) * 127 + 128);
		}
		else{
			g.track.pickups[i].cooldown_timer -= system_tick();
		}
	}
}

void track_face_set_color(track_face_t *face, uint8_t r, uint8_t g, uint8_t b) {
	uint32_t lcol = 0xff000000 | (r<<16) | (g<<8) | b;

	face->vp[0].argb = lcol;
	face->vp[1].argb = lcol;
	face->vp[2].argb = lcol;
	face->vp[3].argb = lcol;

	face->vp[0].oargb = lcol;
}

track_face_t *track_section_get_base_face(section_t *section) {
	return section->base_face;
}

section_t *track_nearest_section(vec3_t pos, vec3_t bias, section_t *section, float *distance) {
	// Start search several sections before current section

	for (int i = 0; i < TRACK_SEARCH_LOOK_BACK; i++) {
		section = section->prev;
	}

	// Find vector from ship center to track section under
	// consideration
	float shortest_distance = 1000000000.0;
	section_t *nearest_section = section;
	section_t *junction = NULL;
	for (int i = 0; i < TRACK_SEARCH_LOOK_AHEAD; i++) {
		if (section->junction) {
			junction = section->junction;
		}

		// Some callers of this function want to de-emphazise the .y component
		// of the difference, hence the multiplication with the bias vector.
		// For the real, exact difference bias should be vec3(1,1,1)
		float d = vec3_len(vec3_mul(vec3_sub(pos, section->center), bias));
		if (d < shortest_distance) {
			shortest_distance = d;
			nearest_section = section;
		}

		section = section->next;
	}

	if (junction) {
		section = junction;
		for (int i = 0; i < TRACK_SEARCH_LOOK_AHEAD; i++) {
			float d = vec3_len(vec3_mul(vec3_sub(pos, section->center), bias));
			if (d < shortest_distance) {
				shortest_distance = d;
				nearest_section = section;
			}

			if (flags_is(junction->flags, SECTION_JUNCTION_START)) {
				section = section->next;
			}
			else {
				section = section->prev;
			}
		}
	}

	if (distance != NULL) {
		*distance = shortest_distance;
	}
	return nearest_section;
}
