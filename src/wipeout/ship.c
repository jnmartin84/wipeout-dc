#include "../mem.h"
#include "../utils.h"
#include "../system.h"

#include "object.h"
#include "scene.h"
#include "track.h"
#include "weapon.h"
#include "camera.h"
#include "image.h"
#include "object.h"
#include "ship.h"
#include "ship_ai.h"
#include "ship_player.h"
#include "game.h"
#include "race.h"
#include "sfx.h"

void ships_load(void) {
	texture_list_t ship_textures = image_get_compressed_textures("wipeout/common/allsh.cmp");
	Object *ship_models = objects_load("wipeout/common/allsh.prm", ship_textures);

	texture_list_t collision_textures = image_get_compressed_textures("wipeout/common/alcol.cmp");
	Object *collision_models = objects_load("wipeout/common/alcol.prm", collision_textures);

	int object_index;
	Object *ship_model = ship_models;
	Object *collision_model = collision_models;

	for (object_index = 0; object_index < len(g.ships) && ship_model && collision_model; object_index++) {
		int ship_index = def.ship_model_to_pilot[object_index];
		g.ships[ship_index].model = ship_model;
		g.ships[ship_index].collision_model = collision_model;

		ship_model = ship_model->next;
		collision_model = collision_model->next;

		ship_init_exhaust_plume(&g.ships[ship_index]);
	}

	error_if(object_index != len(g.ships), "Expected %d ship models, got %d", len(g.ships), object_index);

	uint16_t shadow_textures_start = render_textures_len();
	image_get_texture_semi_trans("wipeout/textures/shad1.tim");
	image_get_texture_semi_trans("wipeout/textures/shad2.tim");
	image_get_texture_semi_trans("wipeout/textures/shad3.tim");
	image_get_texture_semi_trans("wipeout/textures/shad4.tim");

	for (int i = 0; i < len(g.ships); i++) {
		g.ships[i].shadow_texture = shadow_textures_start + (i >> 1);
	}
}

void ships_init(section_t *section) {
	section_t *start_sections[len(g.ships)];

	int ranks_to_pilots[NUM_PILOTS];

	// Initialize ranks with all pilots in order
	for (int i = 0; i < len(g.ships); i++) {
		ranks_to_pilots[i] = i;
	}

	// Randomize order for single race or new championship
	if (g.race_type != RACE_TYPE_CHAMPIONSHIP || g.circut == CIRCUT_ALTIMA_VII) {
		shuffle(ranks_to_pilots, len(ranks_to_pilots));
	}

	// Randomize some tiers in an ongoing championship
	else if (g.race_type == RACE_TYPE_CHAMPIONSHIP) {
		// Initialize with current championship order
		for (int i = 0; i < len(g.ships); i++) {
			ranks_to_pilots[i] = g.championship_ranks[i].pilot;
		}		
		shuffle(ranks_to_pilots, 2); // shuffle 0..1
		shuffle(ranks_to_pilots + 4, len(ranks_to_pilots)-5); // shuffle 4..len-1
	}

	// player is always last
	for (int i = 0; i < len(ranks_to_pilots)-1; i++) {
		if (ranks_to_pilots[i] == g.pilot) {
			swap(ranks_to_pilots[i], ranks_to_pilots[i+1]);
		}
	}

	int start_line_pos = def.circuts[g.circut].settings[g.race_class].start_line_pos;
	for (int i = 0; i < start_line_pos - 15; i++) {
		section = section->next;
	}
	for (int i = 0; i < len(g.ships); i++) {
		start_sections[i] = section;
		section = section->next;
		if ((i % 2) == 0) {
			section = section->next;
		}
	}

	for (int i = 0; i < len(ranks_to_pilots); i++) {
		int rank_inv = (len(g.ships)-1) - i;
		int pilot = ranks_to_pilots[i];
		ship_init(&g.ships[pilot], start_sections[rank_inv], pilot, rank_inv);
	}
}

static inline bool sort_rank_compare(pilot_points_t *pa, pilot_points_t *pb) {
	ship_t *a = &g.ships[pa->pilot];
	ship_t *b = &g.ships[pb->pilot];
	if (a->total_section_num == b->total_section_num) {
		vec3_t c0 = a->section->center;
		vec3_t c1 = a->section->next->center;
		vec3_t dir = vec3_sub(c1, c0);
		float pos_a = vec3_dot(vec3_sub(a->position, c0), dir);
		float pos_b = vec3_dot(vec3_sub(b->position, c0), dir);
		return (pos_a < pos_b);
	}
	else {
		return a->total_section_num < b->total_section_num;
	}
}

void ships_update(void) {
	if (g.race_type == RACE_TYPE_TIME_TRIAL) {
		ship_update(&g.ships[g.pilot]);
	}
	else {
		for (int i = 0; i < len(g.ships); i++) {
			ship_update(&g.ships[i]);
		}
		for (int j = 0; j < (len(g.ships) - 1); j++) {
			for (int i = j + 1; i < len(g.ships); i++) {
				ship_collide_with_ship(&g.ships[i], &g.ships[j]);
			}
		}

		if (flags_is(g.ships[g.pilot].flags, SHIP_RACING)) {
			sort(g.race_ranks, len(g.race_ranks), sort_rank_compare);
			for (int32_t i = 0; i < len(g.ships); i++) {
				g.ships[g.race_ranks[i].pilot].position_rank = i + 1;
			}
		}
	}
}

void ships_reset_exhaust_plumes(void) {
	for (int i = 0; i < len(g.ships); i++) {
		ship_reset_exhaust_plume(&g.ships[i]);
	}
}

void ships_draw(void) {
	uint8_t draw_this = 0;

	// Ship models
	render_set_depth_write(true);
	render_set_depth_test(true);

	for (int i = 0; i < len(g.ships); i++) {
		if (
			(flags_is(g.ships[i].flags, SHIP_VIEW_INTERNAL) && flags_not(g.ships[i].flags, SHIP_IN_RESCUE)) ||
			(g.race_type == RACE_TYPE_TIME_TRIAL && i != g.pilot)
		) {
			continue;
		}

		if (i == g.pilot) {
			draw_this |= (1 << i);
		} else {
#define VIS_TEST_DIST 20
			int distance = VIS_TEST_DIST;
			int sc = g.track.section_count;

			int pilot_secnum = g.ships[g.pilot].section_num;
			int other_secnum = g.ships[i].section_num;

			if (abs(other_secnum - pilot_secnum) < VIS_TEST_DIST) {
				distance = abs(other_secnum - pilot_secnum);
			} else {
				if ((other_secnum - VIS_TEST_DIST) < 0) other_secnum = other_secnum + sc;
				if ((pilot_secnum - VIS_TEST_DIST) < 0) pilot_secnum = pilot_secnum + sc;
				distance = abs(other_secnum - pilot_secnum);
			}

			if (distance < VIS_TEST_DIST) {
				draw_this |= (1 << i);
			}
		}

		ship_draw(&g.ships[i], draw_this & (1 << i));
	}

	// Shadows
	render_set_model_ident();

	render_set_depth_write(false);
	render_set_depth_offset(-32.0);

	for (int i = 0; i < len(g.ships); i++) {
		if (
			(g.race_type == RACE_TYPE_TIME_TRIAL && i != g.pilot) ||
			flags_not(g.ships[i].flags, SHIP_VISIBLE) || 
			flags_is(g.ships[i].flags, SHIP_FLYING) ||
			flags_is(g.ships[i].flags, SHIP_IN_RESCUE)
		) {
			continue;
		}

		ship_draw_shadow(&g.ships[i], draw_this & (1 << i));
	}

	render_set_depth_offset(0.0);
	render_set_depth_write(true);
}

void ship_init(ship_t *self, section_t *section, int pilot, int inv_start_rank) {
	self->pilot = pilot;
	self->velocity = vec3(0, 0, 0);
	self->acceleration = vec3(0, 0, 0);
	self->angle = vec3(0, 0, 0);
	self->angular_velocity = vec3(0, 0, 0);
	self->turn_rate = 0;
	self->thrust_mag = 0;
	self->current_thrust_max = 0;
	self->turn_rate_from_hit = 0;
	self->brake_right = 0;
	self->brake_left = 0;
	self->flags = SHIP_RACING | SHIP_VISIBLE | SHIP_DIRECTION_FORWARD;
	self->weapon_type = WEAPON_TYPE_NONE;
	self->lap = -1;
	self->max_lap = -1;
	self->speed = 0;
	self->ebolt_timer = 0;
	self->revcon_timer = 0;
	self->special_timer = 0;
	self->weapon_target = NULL;

	void *om = mem_bump(sizeof(mat4_t) + 32);
	self->mat = (mat4_t *)(((uintptr_t)om + 31) & ~31);
	self->mat->cols[0][0] = self->mat->cols[1][1] = self->mat->cols[2][2] = self->mat->cols[3][3] = 1.0f;

	self->update_timer = 0;
	self->last_impact_time = 0;

	int team = def.pilots[pilot].team;
	self->mass =          def.teams[team].attributes[g.race_class].mass;
	self->thrust_max =    def.teams[team].attributes[g.race_class].thrust_max;
	self->skid =          def.teams[team].attributes[g.race_class].skid;
	self->turn_rate =     def.teams[team].attributes[g.race_class].turn_rate;
	self->turn_rate_max = def.teams[team].attributes[g.race_class].turn_rate_max;
	self->resistance =    def.teams[team].attributes[g.race_class].resistance;
	self->lap_time = 0;

	self->update_timer = UPDATE_TIME_INITIAL;
	self->position_rank = NUM_PILOTS - inv_start_rank;

	if (pilot == g.pilot) {
		self->update_func = ship_player_update_intro;
		self->remote_thrust_max = 2900;
		self->remote_thrust_mag = 46;
		self->fight_back = 0;
	}
	else {
		self->update_func = ship_ai_update_intro;
		self->remote_thrust_max = def.ai_settings[g.race_class][inv_start_rank-1].thrust_max;
		self->remote_thrust_mag = def.ai_settings[g.race_class][inv_start_rank-1].thrust_magnitude;
		self->fight_back = def.ai_settings[g.race_class][inv_start_rank-1].fight_back;
	}

	self->section = section;
	self->prev_section = section;
	float spread_base = def.circuts[g.circut].settings[g.race_class].spread_base;
	float spread_factor = def.circuts[g.circut].settings[g.race_class].spread_factor;
	int p = inv_start_rank - 1;
	self->start_accelerate_timer = p * (spread_base + (p * spread_factor)) * (1.0/30.0);

	track_face_t *face = g.track.faces + section->face_start;
	face++;
	if ((inv_start_rank % 2) != 0) {
		face++;
	}
	
	vec3_t face_point;// = vec3_mulf(vec3_add(face->tris[0].vertices[0].pos, face->tris[0].vertices[2].pos), 0.5);
	float fpx,fpy,fpz;
	fpx = face->tris[0].vertices[0].x + face->tris[0].vertices[2].x;
	fpx *= 0.5f;
	fpy = face->tris[0].vertices[0].y + face->tris[0].vertices[2].y;
	fpy *= 0.5f;
	fpz = face->tris[0].vertices[0].z + face->tris[0].vertices[2].z;
	fpz *= 0.5f;

	face_point = vec3(fpx,fpy,fpz);

	self->position = vec3_add(face_point, vec3_mulf(face->normal, 200));

	self->section_num = section->num;
	self->prev_section_num = section->num;
	self->total_section_num = section->num;

	section_t *next = section->next;
	vec3_t direction = vec3_sub(next->center, section->center);
	self->angle.y = -bump_atan2f(direction.x, direction.z);
}

void ship_init_exhaust_plume(ship_t *self) {
	int16_t indices[64];
	int16_t indices_len = 0;

	Prm prm = {.primitive = self->model->primitives};

	for (int i = 0; i < self->model->primitives_len; i++) {
		switch (prm.f3->type) {
		case PRM_TYPE_F3 :
			if (flags_is(prm.f3->flag, PRM_SHIP_ENGINE)) {
				die("F3 ::SE marked polys should be ft3's");
			}
			prm.f3 += 1;
			break;

		case PRM_TYPE_F4 :
			if (flags_is(prm.f4->flag, PRM_SHIP_ENGINE)) {
				die("F4 ::SE marked polys should be ft3's");
			}
			prm.f4 += 1;
			break;

		case PRM_TYPE_FT3 :
			if (flags_is(prm.ft3->flag, PRM_SHIP_ENGINE)) {
				indices[indices_len++] = prm.ft3->coords[0];
				indices[indices_len++] = prm.ft3->coords[1];
				indices[indices_len++] = prm.ft3->coords[2];

				flags_add(prm.ft3->flag, PRM_TRANSLUCENT);
				prm.ft3->color = (prm.ft3->color & 0x00FFFFFF) | 0x8C000000;
				prm.ft3->pad1 = 1;
			}
			prm.ft3 += 1;
			break;

		case PRM_TYPE_FT4 :
			if (flags_is(prm.ft4->flag, PRM_SHIP_ENGINE)) {
				die("FT4 ::SE marked polys should be ft3's");
			}
			prm.ft4 += 1;
			break;

		case PRM_TYPE_G3 :
			if (flags_is(prm.g3->flag, PRM_SHIP_ENGINE)) {
				die("G3 ::SE marked polys should be ft3's");
			}
			prm.g3 += 1;
			break;

		case PRM_TYPE_G4 :
			if (flags_is(prm.g4->flag, PRM_SHIP_ENGINE)) {
				die("G4 ::SE marked polys should be ft3's");
			}
			prm.g4 += 1;
			break;

		case PRM_TYPE_GT3 :
			if (flags_is(prm.gt3->flag, PRM_SHIP_ENGINE)) {
				indices[indices_len++] = prm.gt3->coords[0];
				indices[indices_len++] = prm.gt3->coords[1];
				indices[indices_len++] = prm.gt3->coords[2];

				flags_add(prm.gt3->flag, PRM_TRANSLUCENT);
				for (int j = 0; j < 3; j++) {
					prm.gt3->color[j] = (prm.gt3->color[j] & 0x00FFFFFF) | 0x8C000000;
				}
				prm.gt3->pad1 = 1;
			}
			prm.gt3 += 1;
			break;

		case PRM_TYPE_GT4 :
			if (flags_is(prm.gt4->flag, PRM_SHIP_ENGINE)) {
				die("GT4 ::SE marked polys should be ft3's");
			}
			prm.gt4 += 1;
			break;

		default :
			die("cone.c::InitCone:Bad primitive type %x\n", prm.f3->type);
			break;
		}
	}

	// get out the center vertex

	self->exhaust_plume[0].v = NULL;
	self->exhaust_plume[1].v = NULL;
	self->exhaust_plume[2].v = NULL;

	int shared[3] = {-1, -1, -1};
	int booster = 0;
	for (int i = 0; (i < indices_len) && (booster < 3); i++) {
		int similar = 0;
		for (int j = 0; j < indices_len; j++) {
			if (indices[i] == indices[j]) {
				similar++;
				if (similar > 3) {
					int found = 0;
					for (int k = 0; k < 3; k++) {
						if (shared[k] == indices[i]) {
							found = 1;
						}
					}

					if (!found) {
						shared[booster] = indices[i];
						booster++;
					}
				}
			}
		}
	}

	for (int j = 0; j < 3; j++) {
		if (shared[j] != -1) {
			self->exhaust_plume[j].v = (vec3_t *)&self->model->vertices[shared[j]];
			self->exhaust_plume[j].initial.x  = self->model->vertices[shared[j]].x;
			self->exhaust_plume[j].initial.y  = self->model->vertices[shared[j]].y;
			self->exhaust_plume[j].initial.z  = self->model->vertices[shared[j]].z;
		}
	}
}

void ship_reset_exhaust_plume(ship_t* self)
{
	for (int i = 0; i < 3; i++) {
		if (self->exhaust_plume[i].v != NULL) {
			self->exhaust_plume[i].v->z = self->exhaust_plume[i].initial.z;
			self->exhaust_plume[i].v->x = self->exhaust_plume[i].initial.x;
			self->exhaust_plume[i].v->y = self->exhaust_plume[i].initial.y;
		}
	}
}

void ship_draw(ship_t *self, int psec) {
	if (!psec)
		return;

	object_draw(self->model, self->mat);
}

#include <kos.h>
extern pvr_vertex_t vs[5];

void ship_draw_shadow(ship_t *self, int psec) {
	if (!psec)
		return;

	track_face_t *face = track_section_get_base_face(self->section);

	vec3_t face_point = vec3(face->tris[0].vertices[0].x,face->tris[0].vertices[0].y,face->tris[0].vertices[0].z);
	vec3_t nose = vec3_add(self->position, vec3_mulf(self->dir_forward, 384));
	vec3_t wngl = vec3_sub(vec3_sub(self->position, vec3_mulf(self->dir_right, 256)), vec3_mulf(self->dir_forward, 384));
	vec3_t wngr = vec3_sub(vec3_add(self->position, vec3_mulf(self->dir_right, 256)), vec3_mulf(self->dir_forward, 384));

	nose = vec3_sub(nose, vec3_mulf(face->normal, 0.85 * vec3_distance_to_plane(nose, face_point, face->normal)));
	wngl = vec3_sub(wngl, vec3_mulf(face->normal, 0.85 * vec3_distance_to_plane(wngl, face_point, face->normal)));
	wngr = vec3_sub(wngr, vec3_mulf(face->normal, 0.85 * vec3_distance_to_plane(wngr, face_point, face->normal)));

	uint32_t lcol = 0x80000000;

	vec2i_t tsize = render_texture_padsize(self->shadow_texture);
	float rpw = approx_recip((float)tsize.x);
	float rph = approx_recip((float)tsize.y);

	vs[0].flags = PVR_CMD_VERTEX;
	vs[0].x = nose.x;
	vs[0].y = nose.y;
	vs[0].z = nose.z;
	vs[0].u = 64.0f * rpw;
	vs[0].v = 0.0f * rph;
	vs[0].argb = lcol;
	vs[0].oargb = 0;

	vs[1].flags = PVR_CMD_VERTEX;
	vs[1].x = wngr.x;
	vs[1].y = wngr.y;
	vs[1].z = wngr.z;
	vs[1].u = 128.0f * rpw;
	vs[1].v = 256.0f * rph;
	vs[1].argb = lcol;
	vs[1].oargb = 0;

	vs[2].flags = PVR_CMD_VERTEX_EOL;
	vs[2].x = wngl.x;
	vs[2].y = wngl.y;
	vs[2].z = wngl.z;
	vs[2].u = 0 * rpw;
	vs[2].v = 256.0f * rph;
	vs[2].argb = lcol;
	vs[2].oargb = 0;

	render_tri(self->shadow_texture);
}

void ship_update(ship_t *self) {
	// Set Unit vectors of this ship
	float sx = sinf(self->angle.x);
	float cx = cosf(self->angle.x);
	float sy = sinf(self->angle.y);
	float cy = cosf(self->angle.y);
	float sz = sinf(self->angle.z);
	float cz = cosf(self->angle.z);

	self->dir_forward.x = -(sy * cx);
	self->dir_forward.y = - sx;
	self->dir_forward.z =  (cy * cx);

	self->dir_right.x =  (cy * cz) + (sy * sz * sx);
	self->dir_right.y = -(sz * cx);
	self->dir_right.z =  (sy * cz) - (cy * sx * sz);

	self->dir_up.x = (cy * sz) - (sy * sx * cz);
	self->dir_up.y = -(cx * cz);
	self->dir_up.z = (sy * sz) + (cy * sx * cz);

	self->prev_section = self->section;

	// To find the nearest section to the ship, the original source de-emphasizes
	// the .y component when calculating the distance to each section by a 
	// >> 2 shift. I.e. it tries to find the section that is more closely to the
	// horizontal x,z plane (directly underneath the ship), instead of finding 
	// the section with the "real" closest distance. Hence the bias of 
	// vec3(1, 0.25, 1) here.
	float distance;
	self->section = track_nearest_section_qy(self->position, self->section, &distance);
	if (distance > 3700) {
		flags_add(self->flags, SHIP_FLYING);
	}
	else {
		flags_rm(self->flags, SHIP_FLYING);
	}

	self->prev_section_num = self->prev_section->num;
	self->section_num = self->section->num;


	// Figure out which side of the track the ship is on
	track_face_t *face = track_section_get_base_face(self->section);

	vec3_t to_face_vector = vec3(face->tris[0].vertices[0].x - face->tris[0].vertices[1].x,
		face->tris[0].vertices[0].y - face->tris[0].vertices[1].y,
		face->tris[0].vertices[0].z - face->tris[0].vertices[1].z);
//	vec3_sub(
//		face->tris[0].vertices[0].pos,
//		face->tris[0].vertices[1].pos
//	);

	vec3_t direction = vec3_sub(self->section->center, self->position);

	if (vec3_dot(direction, to_face_vector) > 0) {
		flags_add(self->flags, SHIP_LEFT_SIDE);
	}
	else {
		flags_rm(self->flags, SHIP_LEFT_SIDE);
		face++;
	}

	// Collect powerup
	if (
		flags_is(face->flags, FACE_PICKUP_ACTIVE) &&
		flags_not(self->flags, SHIP_SPECIALED) &&
		self->weapon_type == WEAPON_TYPE_NONE &&
		track_collect_pickups(face)
	) {
		if (self->pilot == g.pilot) {
			sfx_play(SFX_POWERUP);
			if (flags_is(self->flags, SHIP_SHIELDED)) {
				self->weapon_type = weapon_get_random_type(WEAPON_CLASS_PROJECTILE);
			}
			else {
				self->weapon_type = weapon_get_random_type(WEAPON_CLASS_ANY);
			}
		}
		else {
			self->weapon_type = 1;
		}
	}

	self->last_impact_time += system_tick();

	// Call the active player/ai update function
	(self->update_func)(self);

	// Animate the exhaust plume

	int exhaust_len;

	if (self->pilot == g.pilot) {
		// get the z exhaust_len related to speed or thrust
		exhaust_len = self->thrust_mag * 0.0625;
		exhaust_len += self->speed * 0.00390625;
	}
	else {
		// for remote ships the z exhaust_len is a constant
		exhaust_len = 150;
	}

	for (int i = 0; i < 3; i++) {
		if (self->exhaust_plume[i].v != NULL) {
			self->exhaust_plume[i].v->z = self->exhaust_plume[i].initial.z - exhaust_len + (rand_int(-16383, 16383) >> 9);
			self->exhaust_plume[i].v->x = self->exhaust_plume[i].initial.x + (rand_int(-16383, 16383) >> 11);
			self->exhaust_plume[i].v->y = self->exhaust_plume[i].initial.y + (rand_int(-16383, 16383) >> 11);
		}
	}

	mat4_set_translation(self->mat, self->position);
	mat4_set_yaw_pitch_roll(self->mat, self->angle);

	// Race position and lap times
	
	self->lap_time += system_tick();

	int start_line_pos = def.circuts[g.circut].settings[g.race_class].start_line_pos;

	// Crossed line backwards
	if (self->prev_section_num == start_line_pos + 1 && self->section_num <= start_line_pos) {
		self->lap--;
	}

	// Crossed line forwards
	else if (self->prev_section_num == start_line_pos && self->section_num > start_line_pos) {
		self->lap++;

		// Is it the first time we're crossing the line for this lap?
		if (self->lap > self->max_lap) {
			self->max_lap = self->lap;

			if (self->lap > 0 && self->lap <= NUM_LAPS) {
				g.lap_times[self->pilot][self->lap-1] = self->lap_time;
			}
			self->lap_time = 0;

			if (g.race_type == RACE_TYPE_TIME_TRIAL) {
				self->weapon_type = WEAPON_TYPE_TURBO;
			}

			if (self->lap == NUM_LAPS && self->pilot == g.pilot) {
				race_end();
			}
		}
	}

	int section_num_from_line = self->section_num - (start_line_pos + 1);
	if (section_num_from_line < 0) {
		section_num_from_line += g.track.section_count;
	}
	self->total_section_num = self->lap * g.track.section_count + section_num_from_line;
}

vec3_t ship_cockpit(ship_t *self) {
	return vec3_add(self->position, vec3_mulf(self->dir_up, 128));
}

vec3_t ship_nose(ship_t *self) {
	return vec3_add(self->position, vec3_mulf(self->dir_forward, 512));
}

vec3_t ship_wing_left(ship_t *self) {
	return vec3_sub(vec3_sub(self->position, vec3_mulf(self->dir_right, 256)), vec3_mulf(self->dir_forward, 256));
}

vec3_t ship_wing_right(ship_t *self) {
	return vec3_sub(vec3_add(self->position, vec3_mulf(self->dir_right, 256)), vec3_mulf(self->dir_forward, 256));
}

static bool vec3_is_on_face(vec3_t pos, track_face_t *face, float alpha) {
	vec3_t plane_point = vec3_sub(pos, vec3_mulf(face->normal, alpha));
	float fx,fy,fz;
	fx = face->tris[0].vertices[1].x;
	fy = face->tris[0].vertices[1].y;
	fz = face->tris[0].vertices[1].z;
	vec3_t facev = vec3(fx,fy,fz);

	vec3_t vec0 = vec3_sub(plane_point, facev);//face->tris[0].vertices[1].pos);
	fx = face->tris[0].vertices[2].x;
	fy = face->tris[0].vertices[2].y;
	fz = face->tris[0].vertices[2].z;
	facev = vec3(fx,fy,fz);

	vec3_t vec1 = vec3_sub(plane_point, facev);//face->tris[0].vertices[2].pos);

	fx = face->tris[0].vertices[0].x;
	fy = face->tris[0].vertices[0].y;
	fz = face->tris[0].vertices[0].z;
	facev = vec3(fx,fy,fz);

	vec3_t vec2 = vec3_sub(plane_point, facev);//face->tris[0].vertices[0].pos);

	fx = face->tris[1].vertices[0].x;
	fy = face->tris[1].vertices[0].y;
	fz = face->tris[1].vertices[0].z;
	facev = vec3(fx,fy,fz);

	vec3_t vec3 = vec3_sub(plane_point, facev);//face->tris[1].vertices[0].pos);

	float angle = 
		vec3_angle(vec0, vec2) +
		vec3_angle(vec2, vec3) +
		vec3_angle(vec3, vec1) +
		vec3_angle(vec1, vec0);

	return (angle > 5.7524279545711546114428284605948); // twopi_i754 * 0.91f;
}

void ship_resolve_wing_collision(ship_t *self, track_face_t *face, float direction) {
	vec3_t triv = vec3(face->tris[0].vertices[2].x, face->tris[0].vertices[2].y, face->tris[0].vertices[2].z);
	vec3_t collision_vector = vec3_sub(self->section->center, triv);//face->tris[0].vertices[2].pos);

	//collisionVector.vx = playerShip->nearTrkSect->centre.vx - track->vertices[(facePtr->vertex[2])].vx;
	//collisionVector.vy = playerShip->nearTrkSect->centre.vy - track->vertices[(facePtr->vertex[2])].vy;
	//collisionVector.vz = playerShip->nearTrkSect->centre.vz - track->vertices[(facePtr->vertex[2])].vz;
	//angle = AngleOfCollision(collisionVector, playerShip->unitVecNose);

	float angle = vec3_angle(collision_vector, self->dir_forward);

	self->velocity = vec3_reflect(self->velocity, face->normal);
	// ppivot = ppivot - (vpivot / 64)
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625f)); // system_tick?
	// vpivot = vpivot - (vpivot / 2)
	self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.5f));
	// vpivot = vpivot + face->normal
	self->velocity = vec3_add(self->velocity, vec3_mulf(face->normal , 4096.0f)); // div by 4096?

	float magnitude = (fabsf(angle) * self->speed) * 0.00153398078788564122971808758949f;
//	((2.0f * F_PI) / (4096.0f * 2.0f)); // (6 velocity shift, 12 angle shift?)

	vec3_t wing_pos;
	if (direction > 0) {
		self->angular_velocity.z += magnitude;
		wing_pos = vec3_add(self->position, vec3_mulf(vec3_sub(self->dir_right, self->dir_forward), 256.0f)); // >> 4??
	}
	else {
		self->angular_velocity.z -= magnitude;	
		wing_pos = vec3_sub(self->position, vec3_mulf(vec3_sub(self->dir_right, self->dir_forward), 256.0f)); // >> 4??
	}

	if (self->last_impact_time > 0.2f) {
		self->last_impact_time = 0.0f;
		sfx_play_at(SFX_IMPACT, wing_pos, vec3(0.0f, 0.0f, 0.0f), 1.0f);
	}
}


void ship_resolve_nose_collision(ship_t *self, track_face_t *face, float direction) {
	self->velocity = vec3_reflect(self->velocity, face->normal);
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625f)); // system_tick?
	self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.5f));
	self->velocity = vec3_add(self->velocity, vec3_mulf(face->normal, 4096.0f)); // div by 4096?

	float magnitude = ((self->speed * 0.0625f) + 400.0f) * 0.00153398078788564122971808758949f;
	//2.0f * F_PI / 4096.0f;
	if (direction > 0) {
		self->angular_velocity.y += magnitude;
	}
	else { 
		self->angular_velocity.y -= magnitude;
	}

	if (self->last_impact_time > 0.2f) {
		self->last_impact_time = 0.0f;
		sfx_play_at(SFX_IMPACT, ship_nose(self), vec3(0.0f, 0.0f, 0.0f), 1.0f);
	}
}


void ship_collide_with_track(ship_t *self, track_face_t *face) {
	float alpha;
	section_t 	*trackPtr;
	bool collide;
	track_face_t *face2;

	trackPtr = self->section->next;
	vec3_t direction = vec3_sub(trackPtr->center, self->section->center);
	float down_track = vec3_dot(direction, self->dir_forward);

	if (down_track < 0) {
		flags_rm(self->flags, SHIP_DIRECTION_FORWARD);
	}
	else {
		flags_add(self->flags, SHIP_DIRECTION_FORWARD);
	}

	vec3_t v0 = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);
	vec3_t v1 = vec3(face->tris[0].vertices[1].x, face->tris[0].vertices[1].y, face->tris[0].vertices[1].z);
	vec3_t to_face_vector = vec3_sub(v0, v1);//face->tris[0].vertices[0].pos, face->tris[0].vertices[1].pos);
	direction = vec3_sub(self->section->center, self->position);
	float to_face = vec3_dot(direction, to_face_vector);

	face--;

	// Check against left hand side of track
	
	// FIXME: the collision checks in junctions are very flakey and often select
	// the wrong face to test for a collision.
	// Instead of this whole mess here, there should just be a function 
	// `track_get_nearest_face(section, pos)` that we call with the nose and 
	// wing positions and then just resolve against this face.

	if (to_face > 0.0f) {
		flags_add(self->flags, SHIP_LEFT_SIDE);
		//face->tris[0].vertices[0].pos;
		vec3_t face_point = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);

		alpha = vec3_distance_to_plane(ship_nose(self), face_point, face->normal);
		if (alpha <= 0.0f) {
			if (flags_is(self->section->flags, SECTION_JUNCTION_START)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->next->face_start;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else if (flags_is(self->section->flags, SECTION_JUNCTION_END)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->prev->face_start;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else {
				ship_resolve_nose_collision(self, face, -down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_left(self), face_point, face->normal);
		if (alpha <= 0.0f) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) || 
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_left(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, -down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_right(self), face_point, face->normal);
		if (alpha <= 0) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) ||
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_right(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, -down_track);
			}
			return;
		}
	}

	// Collision check against 2nd wall
	else {
		flags_rm(self->flags, SHIP_LEFT_SIDE);

		face++;
		while (face->flags & FACE_TRACK_BASE) {
			face++;
		}

		//face->tris[0].vertices[0].pos;
		vec3_t face_point = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);
		alpha = vec3_distance_to_plane(ship_nose(self), face_point, face->normal);
		if (alpha <= 0.0f) {
			if (flags_is(self->section->flags, SECTION_JUNCTION_START)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
				else {
					face2 = g.track.faces + self->section->next->face_start;
					face2 += 3;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face, -down_track);
					}
				}
			}
			else if (flags_is(self->section->flags, SECTION_JUNCTION_END)) {
				collide = vec3_is_on_face(ship_nose(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, -down_track);
				}
				else {
					face2 = g.track.faces + self->section->prev->face_start;
					face2 += 3;
					collide = vec3_is_on_face(ship_nose(self), face2, alpha);
					if (collide) {
						ship_resolve_nose_collision(self, face2, -down_track);
					}
				}
			}
			else {
				ship_resolve_nose_collision(self, face, down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_left(self), face_point, face->normal);
		if (alpha <= 0.0f) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) ||
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_left(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, down_track);
			}
			return;
		}

		alpha = vec3_distance_to_plane(ship_wing_right(self), face_point, face->normal);
		if (alpha <= 0.0f) {
			if (
				flags_is(self->section->flags, SECTION_JUNCTION_START) ||
				flags_is(self->section->flags, SECTION_JUNCTION_END)
			) {
				collide = vec3_is_on_face(ship_wing_right(self), face, alpha);
				if (collide) {
					ship_resolve_nose_collision(self, face, down_track);
				}
			}
			else {
				ship_resolve_wing_collision(self, face, down_track);
			}
			return;
		}
	}
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
bool ship_intersects_ship(ship_t *self, ship_t *other) {
	// Get 4 points of collision model relative to the camera
	fast_mat_load(&other->mat->cols);
	vec3_t a = vector_transform(other->collision_model->vertices[0]);
	vec3_t b = vector_transform(other->collision_model->vertices[1]);
	vec3_t c = vector_transform(other->collision_model->vertices[2]);
	vec3_t d = vector_transform(other->collision_model->vertices[3]);

	vec3_t other_points[6] = {b, a, d, a, a, b};
	vec3_t other_lines[6] = {
		vec3_sub(c, b),
		vec3_sub(c, a),
		vec3_sub(c, d),
		vec3_sub(b, a),
		vec3_sub(d, a),
		vec3_sub(d, b)
	};

	Prm poly = {.primitive = other->collision_model->primitives};
	int primitives_len = other->collision_model->primitives_len;

	vec3_t p1, p2, p3;
	fast_mat_load(&self->mat->cols);
	// for all 4 planes of the enemy ship
	for (int pi = 0; pi < primitives_len; pi++) {
		int16_t *indices;
		switch (poly.primitive->type) {
			case PRM_TYPE_F3:
				indices = poly.f3->coords;
				p1 =  vector_transform(self->collision_model->vertices[indices[0]]);
				p2 =  vector_transform(self->collision_model->vertices[indices[1]]);
				p3 =  vector_transform(self->collision_model->vertices[indices[2]]);
				poly.f3++;
				break;
			case PRM_TYPE_G3:
				indices = poly.g3->coords;
				p1 =  vector_transform(self->collision_model->vertices[indices[0]]);
				p2 =  vector_transform(self->collision_model->vertices[indices[1]]);
				p3 =  vector_transform(self->collision_model->vertices[indices[2]]);
				poly.g3++;
				break;
			case PRM_TYPE_FT3:
				indices = poly.ft3->coords;
				p1 =  vector_transform(self->collision_model->vertices[indices[0]]);
				p2 =  vector_transform(self->collision_model->vertices[indices[1]]);
				p3 =  vector_transform(self->collision_model->vertices[indices[2]]);
				poly.ft3++;
				break;
			case PRM_TYPE_GT3:
				indices = poly.gt3->coords;
				p1 =  vector_transform(self->collision_model->vertices[indices[0]]);
				p2 =  vector_transform(self->collision_model->vertices[indices[1]]);
				p3 =  vector_transform(self->collision_model->vertices[indices[2]]);
				poly.gt3++;
				break;
			default:
				break;
		}

		// Find polyGon line vectors
		vec3_t p1p2 = vec3_sub(p2, p1);
		vec3_t p1p3 = vec3_sub(p3, p1);

		// Find plane equations
		vec3_t plane1 = vec3_cross(p1p2, p1p3);

		for (int vi = 0; vi < 6; vi++) {
			float dp1 = vec3_dot(vec3_sub(p1, other_points[vi]), plane1);
			float dp2 = vec3_dot(other_lines[vi], plane1);

			if (dp2 != 0) {
				float norm = dp1 * copysignf(approx_recip(dp2), dp2);

				if ((norm >= 0) && (norm <= 1)) {
					vec3_t term = vec3_mulf(other_lines[vi], norm);
					vec3_t res = vec3_add(term, other_points[vi]);

					vec3_t v0 = vec3_sub(p1, res);
					vec3_t v1 = vec3_sub(p2, res);
					vec3_t v2 = vec3_sub(p3, res);

					float angle =
						vec3_angle(v0, v1) +
						vec3_angle(v1, v2) +
						vec3_angle(v2, v0);

					// 2pi plus wiggle room
					if ((angle >= M_PI * 2 - M_PI * 0.1)) {
						return true;
					}
				}
			}
		}
	}

	return false;
}

void ship_collide_with_ship(ship_t *self, ship_t *other) {
	float distance = vec3_len(vec3_sub(self->position, other->position));

	// Do a quick distance check; if ships are far apart, remove the collision flag
	// and early out.
	if (distance > 960) {
		flags_rm(self->flags, SHIP_COLL);
		flags_rm(other->flags, SHIP_COLL);
		return;
	}

	// Ships are close, do a real collision test
	if (!ship_intersects_ship(self, other)) {
		return;
	}

	// Ships did collide, resolve

	vec3_t vc = vec3_divf(
		vec3_add(
			vec3_mulf(self->velocity, self->mass),
			vec3_mulf(other->velocity, other->mass)
		),
		self->mass + other->mass
	);

	vec3_t ship_react = vec3_mulf(vec3_sub(vc, self->velocity), 0.5f); // >> 1
	vec3_t other_react = vec3_mulf(vec3_sub(vc, other->velocity), 0.5f); // >> 1
	self->position = vec3_sub(self->position, vec3_mulf(self->velocity, 0.015625f)); // >> 6
	other->position = vec3_sub(other->position, vec3_mulf(other->velocity, 0.015625f)); // >> 6

	self->velocity = vec3_add(vc, ship_react);
	other->velocity = vec3_add(vc, other_react);

	vec3_t res = vec3_sub(self->position, other->position);

	self->velocity = vec3_add(self->velocity, vec3_mulf(res, 4));  // << 2
	self->position = vec3_add(self->position, vec3_mulf(self->velocity, 0.015625f)); // >> 6

	other->velocity = vec3_sub(other->velocity, vec3_mulf(res, 4)); // << 2
	other->position = vec3_add(other->position, vec3_mulf(other->velocity, 0.015625f)); // >> 6

	if (
		flags_not(self->flags, SHIP_COLL) &&
		flags_not(other->flags, SHIP_COLL) &&
		self->last_impact_time > 0.2f
	) {
		self->last_impact_time = 0;
		vec3_t sound_pos = vec3_mulf(vec3_add(self->position, other->position), 0.5f);
		sfx_play_at(SFX_CRUNCH, sound_pos, vec3(0.0f, 0.0f, 0.0f), 1.0f);
	}
	flags_add(self->flags, SHIP_COLL);
	flags_add(other->flags, SHIP_COLL);
}
