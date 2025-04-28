#include <stdint.h>
#include <math.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "../mem.h"
#include "object.h"
#include "track.h"
#include "ship.h"
#include "weapon.h"
#include "hud.h"
#include "droid.h"
#include "camera.h"
#include "../utils.h"
#include "scene.h"


#include "../input.h"
#include "../system.h"

#include "sfx.h"
#include "ship_player.h"
#include "ship_ai.h"
#include "game.h"
#include "particle.h"
void sfx_update_ex(sfx_t *sfx);
extern void SetShake(float shake);
void ship_player_update_sfx(ship_t *self) {
#if 0
	float speedf = self->speed * 0.0000152587890625f;//0.000015f;
	self->sfx_engine_intake->volume = clamp(speedf * 4.0f, 0.0f, 1.0f);
	self->sfx_engine_intake->pitch = 0.5f + speedf * 1.25f;

	self->sfx_engine_thrust->volume = clamp(0.05f + 0.025f * (self->thrust_mag / self->thrust_max) * 8.0f/* * 2.0 */, 0.0f, 1.0f);
	self->sfx_engine_thrust->pitch = 0.2f + 0.5f * (self->thrust_mag / self->thrust_max) + speedf;

	float brake_left = self->brake_left * 0.0035f;
	float brake_right = self->brake_right * 0.0035f;
	self->sfx_turbulence->volume = (speedf * brake_left + speedf * brake_right) * 4.0f;//2.5f;//1.5f;
	self->sfx_turbulence->pan = (brake_right - brake_left);

	self->sfx_shield->volume = flags_is(self->flags, SHIP_SHIELDED) ? 1.0f : 0.0f;

	sfx_update_ex(self->sfx_engine_intake);
	sfx_update_ex(self->sfx_engine_thrust);
	sfx_update_ex(self->sfx_turbulence);
	sfx_update_ex(self->sfx_shield);
#endif	
	float speedf = self->speed * 0.000015f;
	self->sfx_engine_intake->volume = clamp(speedf * 1.0f, 0.0f, 1.0f);
	self->sfx_engine_intake->pitch = 0.5f + speedf * 1.25f;

	self->sfx_engine_thrust->volume = clamp((0.05f + 0.025f * (self->thrust_mag / self->thrust_max)) * 1.0f, 0.0f, 1.0f);
	self->sfx_engine_thrust->pitch = 0.2f + 0.5f * (self->thrust_mag / self->thrust_max) + speedf;

	float brake_left = self->brake_left * 0.0035f;
	float brake_right = self->brake_right * 0.0035f;
	self->sfx_turbulence->volume = clamp((speedf * brake_left + speedf * brake_right) * 2.0f, 0.0f, 1.0f);
	self->sfx_turbulence->pan = (brake_right - brake_left);

	self->sfx_shield->volume = flags_is(self->flags, SHIP_SHIELDED) ? 0.75f : 0.0f;

	sfx_update_ex(self->sfx_engine_intake);
	sfx_update_ex(self->sfx_engine_thrust);
	sfx_update_ex(self->sfx_turbulence);
	sfx_update_ex(self->sfx_shield);
}

void ship_player_update_intro(ship_t *self) {
	self->temp_target = self->position;

	self->sfx_engine_thrust = sfx_reserve_loop(SFX_ENGINE_THRUST);
	self->sfx_engine_intake = sfx_reserve_loop(SFX_ENGINE_INTAKE);
	self->sfx_shield = sfx_reserve_loop(SFX_SHIELD);
	self->sfx_turbulence = sfx_reserve_loop(SFX_TURBULENCE);

	ship_player_update_intro_general(self);
	self->update_func = ship_player_update_intro_await_three;
}

void ship_player_update_intro_await_three(ship_t *self) {
	ship_player_update_intro_general(self);

	if (self->update_timer <= UPDATE_TIME_THREE) {
		/* sfx_t *sfx = */ sfx_play(SFX_VOICE_COUNT_3);
		self->update_func = ship_player_update_intro_await_two;
	}
}

void ship_player_update_intro_await_two(ship_t *self) {
	ship_player_update_intro_general(self);	

	if (self->update_timer <= UPDATE_TIME_TWO) {
		scene_set_start_booms(1);
		/* sfx_t *sfx = */ sfx_play(SFX_VOICE_COUNT_2);
		self->update_func = ship_player_update_intro_await_one;
	}
}

void ship_player_update_intro_await_one(ship_t *self) {
	ship_player_update_intro_general(self);

	if (self->update_timer <= UPDATE_TIME_ONE) {
		scene_set_start_booms(2);
		/* sfx_t *sfx = */ sfx_play(SFX_VOICE_COUNT_1);
		self->update_func = ship_player_update_intro_await_go;
	}
}

void ship_player_update_intro_await_go(ship_t *self) {
	ship_player_update_intro_general(self);

	if (self->update_timer <= UPDATE_TIME_GO) {
		scene_set_start_booms(3);
		/* sfx_t *sfx = */ sfx_play(SFX_VOICE_COUNT_GO);
		
		if (flags_is(self->flags, SHIP_RACING)) {
			// Check for stall
			if (self->thrust_mag >= 680 && self->thrust_mag <= 700) {
				self->thrust_mag = 1800;
				self->current_thrust_max = 1800;
			}
			else if (self->thrust_mag < 680) {
				self->current_thrust_max = self->thrust_max;
			}
			else {
				self->current_thrust_max = 200;
			}

			self->update_timer = UPDATE_TIME_STALL;
			self->update_func = ship_player_update_race;
		}
		else {
			self->update_func = ship_ai_update_race;
		}
	}
}

void ship_player_update_intro_general(ship_t *self) {
	self->update_timer -= system_tick();
	self->position.y = self->temp_target.y + sinf(self->update_timer * 80.0 * 30.0 * twopi_i754 / 4096.0) * 32;

	// Thrust
	if (input_state(A_THRUST)) {
		self->thrust_mag += input_state(A_THRUST) * SHIP_THRUST_RATE * system_tick();
	}
	else {
		self->thrust_mag -= SHIP_THRUST_RATE * system_tick();
	}

	self->thrust_mag = clamp(self->thrust_mag, 0, self->thrust_max);

	// View
	if (input_pressed(A_CHANGE_VIEW)) {
		if (flags_not(self->flags, SHIP_VIEW_INTERNAL)) {
			g.camera.update_func = camera_update_race_internal;
			flags_add(self->flags, SHIP_VIEW_INTERNAL);
		}
		else {
			g.camera.update_func = camera_update_race_external;
			flags_rm(self->flags, SHIP_VIEW_INTERNAL);
		}
	}

	ship_player_update_sfx(self);
}


void ship_player_update_race(ship_t *self) {
	if (flags_not(self->flags, SHIP_RACING)) {
		self->update_func = ship_ai_update_race;
		return;
	}

	if (self->ebolt_timer > 0) {
		self->ebolt_timer -= system_tick();
	}

	if (self->ebolt_timer <= 0) {
		flags_rm(self->flags, SHIP_ELECTROED);
	}

	if (self->revcon_timer > 0) {
		self->revcon_timer -= system_tick();
	}

	if (self->revcon_timer <= 0) {
		flags_rm(self->flags, SHIP_REVCONNED);
	}

	if (self->special_timer > 0) {
		self->special_timer -= system_tick();
	}

	if (self->special_timer <= 0) {
		flags_rm(self->flags, SHIP_SPECIALED);
	}

	if (flags_is(self->flags, SHIP_REVCONNED)) {
		// FIXME_PL: make sure revconned is honored
	}

	// Turning
	// For analog input we set a turn_target (exponentiated by the analog_response 
	// curve) and stop adding to angular acceleration once we exceed that target. 
	// For digital input (0|1) the powf() and multiplication with turn_rate_max 
	// will have no influence on the original behavior.
	self->angular_acceleration = vec3(0, 0, 0);

	if (input_state(A_LEFT)) {
		if (self->angular_velocity.y < 0) {
			self->angular_acceleration.y += self->turn_rate * 2;
		}
		else {
			float turn_target = powf(input_state(A_LEFT), save.analog_response);
			if (turn_target * self->turn_rate_max > self->angular_velocity.y) {
				self->angular_acceleration.y += self->turn_rate;
			}
		}
	}
	else if (input_state(A_RIGHT)) {
		if (self->angular_velocity.y > 0) {
			self->angular_acceleration.y -= self->turn_rate * 2;
		}
		else {
			float turn_target = powf(input_state(A_RIGHT), save.analog_response);
			if (turn_target * -self->turn_rate_max < self->angular_velocity.y) {	
				self->angular_acceleration.y -= self->turn_rate;
			}
		}
	}
	
	if (flags_is(self->flags, SHIP_ELECTROED)) {
		self->ebolt_effect_timer += system_tick();

		// Yank the ship every 0.1 seconds
		if (self->ebolt_effect_timer > 0.1) {
			self->ebolt_effect_timer -= 0.1;
			if (flags_is(self->flags, SHIP_VIEW_INTERNAL)) {
				SetShake(2);
			}
			self->angular_velocity.y += rand_float(-0.5f, 0.5f);

			if (rand_int(0, 10) == 0) { // approx once per second
				self->thrust_mag -= self->thrust_mag * 0.25;
			}
		}
	}

	self->angular_acceleration.x += input_state(A_DOWN) * SHIP_PITCH_ACCEL;
	self->angular_acceleration.x -= input_state(A_UP) * SHIP_PITCH_ACCEL;

	// Handle Stall
	if (self->update_timer > 0.0f) {
		if (self->current_thrust_max < 500.0f) {
			self->current_thrust_max += rand_float(0.0f, 165.0f) * system_tick();
		}
		self->update_timer -= system_tick();
	}
	else {
		// End stall / boost
		self->current_thrust_max = self->thrust_max;
	}

	// Thrust
	if (input_state(A_THRUST)) {
		self->thrust_mag += input_state(A_THRUST) * SHIP_THRUST_RATE * system_tick();
	}
	else {
		self->thrust_mag -= SHIP_THRUST_FALLOFF * system_tick();
	}
	self->thrust_mag = clamp(self->thrust_mag, 0.0f, self->current_thrust_max);

	// Brake
	if (input_state(A_BRAKE_RIGHT))	{
		self->brake_right += SHIP_BRAKE_RATE * system_tick();
	}
	else if (self->brake_right > 0.0f) {
		self->brake_right -= SHIP_BRAKE_RATE * system_tick();
	}
	self->brake_right = clamp(self->brake_right, 0.0f, 256.0f);

	if (input_state(A_BRAKE_LEFT))	{
		self->brake_left += SHIP_BRAKE_RATE * system_tick();
	}
	else if (self->brake_left > 0) {
		self->brake_left -= SHIP_BRAKE_RATE * system_tick();
	}
	self->brake_left = clamp(self->brake_left, 0.0f, 256.0f);

	// View
	if (input_pressed(A_CHANGE_VIEW)) {
		if (flags_not(self->flags, SHIP_VIEW_INTERNAL)) {
			g.camera.update_func = camera_update_race_internal;
			flags_add(self->flags, SHIP_VIEW_INTERNAL);
		}
		else {
			g.camera.update_func = camera_update_race_external;
			flags_rm(self->flags, SHIP_VIEW_INTERNAL);
		}
	}

	if (self->weapon_type == WEAPON_TYPE_MISSILE || self->weapon_type == WEAPON_TYPE_EBOLT) {
		self->weapon_target = ship_player_find_target(self);
	}
	else {
		self->weapon_target = NULL;
	}

	// Fire

	if (input_pressed(A_FIRE) && self->weapon_type != WEAPON_TYPE_NONE) {
		if (flags_not(self->flags, SHIP_SHIELDED)) {
			weapons_fire(self, self->weapon_type);
		}
		else {
			sfx_play(SFX_MENU_MOVE);
		}
	}

	// Physics

	// Calculate thrust vector along principle axis of ship
	self->thrust = vec3_mulf(self->dir_forward, self->thrust_mag * 64.0f);
	self->speed = vec3_len(self->velocity);
	vec3_t forward_velocity = vec3_mulf(self->dir_forward, self->speed);

	// SECTION_JUMP
	if (flags_is(self->section->flags, SECTION_JUMP)) {
		track_face_t *face = track_section_get_base_face(self->section);

		// Project the ship's position to the track section using the face normal.
		// If the point lands on the track, the sum of the angles between the 
		// point and the track vertices will be M_PI*2.
		// If it's less then M_PI*2 (minus a safety margin) we are flying!

		//vec3_t face_point = face->tris[0].vertices[0].pos;
		vec3_t face_point = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);
		vec3_t fp1 = vec3(face->tris[0].vertices[1].x, face->tris[0].vertices[1].y, face->tris[0].vertices[1].z);
		vec3_t fp2 = vec3(face->tris[0].vertices[2].x, face->tris[0].vertices[2].y, face->tris[0].vertices[2].z);
		vec3_t fp3 = vec3(face->tris[1].vertices[0].x, face->tris[1].vertices[0].y, face->tris[1].vertices[0].z);

		float height = vec3_distance_to_plane(self->position, face_point,  face->normal);

		vec3_t plane_point = vec3_sub(self->position, vec3_mulf(face->normal, height));
		vec3_t vec0 = vec3_sub(plane_point, fp1);//face->tris[0].vertices[1].pos);
		vec3_t vec1 = vec3_sub(plane_point, fp2);//face->tris[0].vertices[2].pos);

		face++;

		face_point = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);
		fp3 = vec3(face->tris[1].vertices[0].x, face->tris[1].vertices[0].y, face->tris[1].vertices[0].z);

		vec3_t vec2 = vec3_sub(plane_point, face_point);//face->tris[0].vertices[0].pos);
		vec3_t vec3 = vec3_sub(plane_point, fp3);//face->tris[1].vertices[0].pos);

		float angle = 
			vec3_angle(vec0, vec2) +
			vec3_angle(vec2, vec3) +
			vec3_angle(vec3, vec1) +
			vec3_angle(vec1, vec0);

		// < 30000
		if (angle < 5.7524279545711546114428284605948f) { //(0.91552734375f * twopi_i754)) {
			flags_add(self->flags, SHIP_FLYING);
		}
	}

	// Held by track
	if (flags_not(self->flags, SHIP_FLYING)) {
		track_face_t *face = track_section_get_base_face(self->section);
		ship_collide_with_track(self, face);

		if (flags_not(self->flags, SHIP_LEFT_SIDE)) {
			face++;
		}

		// Boost
		if (flags_not(self->flags, SHIP_SPECIALED) && flags_is(face->flags, FACE_BOOST)) {
			vec3_t track_direction = vec3_sub(self->section->next->center, self->section->center);
			self->velocity = vec3_add(self->velocity, vec3_mulf(track_direction, 30.0f * system_tick()));
		}

		//vec3_t face_point = face->tris[0].vertices[0].pos;
		vec3_t face_point = vec3(face->tris[0].vertices[0].x, face->tris[0].vertices[0].y, face->tris[0].vertices[0].z);
		float height = vec3_distance_to_plane(self->position, face_point, face->normal);

		// Collision with floor
		if (height <= 0.0f) {
			if (self->last_impact_time > 0.2f) {
				self->last_impact_time = 0.0f;
				sfx_play_at(SFX_IMPACT, self->position, vec3(0.0f,0.0f,0.0f), 1.0f);
			}
			self->velocity = vec3_reflect(self->velocity, face->normal);
			self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.125f));
			self->velocity = vec3_sub(self->velocity, vec3_mulf(face->normal, 64.0f * 30.0f * system_tick()));
		}
		else if (height < 30.0f) {
			self->velocity = vec3_add(self->velocity, vec3_mulf(face->normal, 64.0f * 30.0f * system_tick()));
		}

		if (height < 50.0f) {
			height = 50.0f;
		}

		// Calculate acceleration
		float brake = (self->brake_left + self->brake_right);
		float resistance = (self->resistance * (SHIP_MAX_RESISTANCE - (brake * 0.125f))) * 0.0078125f;

		vec3_t force = vec3(0, SHIP_ON_TRACK_GRAVITY, 0.0f);
		force = vec3_add(force, vec3_mulf(vec3_mulf(face->normal, 4096.0f), (SHIP_TRACK_MAGNET * SHIP_TRACK_FLOAT) * approx_recip(height))); // / height));
		force = vec3_sub(force, vec3_mulf(vec3_mulf(face->normal, 4096.0f), SHIP_TRACK_MAGNET));
		force = vec3_add(force, self->thrust);

		self->acceleration = vec3_divf(vec3_sub(forward_velocity, self->velocity), self->skid + brake * 0.25f);
		self->acceleration = vec3_add(self->acceleration, vec3_divf(force, self->mass));
		self->acceleration = vec3_sub(self->acceleration, vec3_divf(self->velocity, resistance));

		// Burying the nose in the track? Move it out!
		vec3_t nose_pos = vec3_add(self->position, vec3_mulf(self->dir_forward, 128.0f));
		float nose_height = vec3_distance_to_plane(nose_pos,face_point, face->normal);
		// vpitch
		if (nose_height < 600.0f) {
			self->angular_acceleration.x += NTSC_ACCELERATION(ANGLE_NORM_TO_RADIAN(FIXED_TO_FLOAT((height - nose_height + 5.0f) * 0.0625f/* (1.0/16.0) */)));
		}
		else {
			self->angular_acceleration.x += NTSC_ACCELERATION(ANGLE_NORM_TO_RADIAN(FIXED_TO_FLOAT(-3.125f/* -50.0/16.0 */)));
		}
	}

	// Flying
	else {
		// Detect the need for a rescue droid
		section_t *next = self->section->next;

		vec3_t best_path = vec3_project_to_ray(self->position, next->center, self->section->center);
		vec3_t distance = vec3_sub(best_path, self->position);

		if (distance.y > -512.0f) {
			distance.y = distance.y * 0.0001f;
		}
		else {
			distance = vec3_mulf(distance, 8.0f);
		}

		// Do we need to be rescued?
		if (vec3_len(distance) > 8000) {
			self->update_func = ship_player_update_rescue;
			self->update_timer = UPDATE_TIME_RESCUE;
			flags_add(self->flags, SHIP_IN_RESCUE | SHIP_FLYING);

			section_t *landing = self->section->prev;
			for (int i = 0; i < 3; i++) {
				landing = landing->prev;
			}
			for (int i = 0; i < 10 && flags_not(landing->flags, SECTION_JUMP); i++) {
				landing = landing->next;
			}
			self->section = landing;
			self->temp_target = vec3_mulf(vec3_add(landing->center, landing->next->center), 0.5f);
			self->temp_target.y -= 2000.0f;
			self->velocity = vec3(0.0f, 0.0f, 0.0f);
		}


		float brake = (self->brake_left + self->brake_right);
		float resistance = (self->resistance * (SHIP_MAX_RESISTANCE - (brake * 0.125f))) * 0.0078125f;

		vec3_t force = vec3(0.0f, SHIP_FLYING_GRAVITY, 0.0f);
		force = vec3_add(force, self->thrust);

		self->acceleration = vec3_divf(vec3_sub(forward_velocity, self->velocity), SHIP_MIN_RESISTANCE + brake * 4.0f);
		self->acceleration = vec3_add(self->acceleration, vec3_divf(force, self->mass));
		self->acceleration = vec3_sub(self->acceleration, vec3_divf(self->velocity, resistance));

		self->angular_acceleration.x += NTSC_ACCELERATION(ANGLE_NORM_TO_RADIAN(FIXED_TO_FLOAT(-3.125f/* -50.0f/16.0f */)));
	}

	// Position
	self->velocity = vec3_add(self->velocity, vec3_mulf(self->acceleration, 30.0f * system_tick()));
	self->position = vec3_add(self->position, vec3_mulf(self->velocity, 0.015625f * 30.0f * system_tick()));

	self->angular_acceleration.x -= self->angular_velocity.x * 0.25f * 30.0f;
	self->angular_acceleration.z += (self->angular_velocity.y - (0.5f * self->angular_velocity.z)) * 30.0f;

	// Orientation
	if (self->angular_acceleration.y == 0.0f) {
		float reciptick = approx_recip(system_tick());
		if (self->angular_velocity.y > 0.0f) {
			self->angular_acceleration.y -= fminf(self->turn_rate, self->angular_velocity.y * reciptick);// / system_tick());
		}
		else if (self->angular_velocity.y < 0.0f) {
			self->angular_acceleration.y += fminf(self->turn_rate, -self->angular_velocity.y * reciptick);// / system_tick());
		}
	}

	self->angular_velocity = vec3_add(self->angular_velocity, vec3_mulf(self->angular_acceleration, system_tick()));
	self->angular_velocity.y = clamp(self->angular_velocity.y, -self->turn_rate_max, self->turn_rate_max);
	
	float brake_dir = (self->brake_left - self->brake_right) * 0.000030517578125f;//(0.125 / 4096.0);
	self->angle.y += brake_dir * self->speed * 0.000030517578125f * twopi_i754 * 30.0f * system_tick();

	self->angle = vec3_add(self->angle, vec3_mulf(self->angular_velocity, system_tick()));
	self->angle.z -= self->angle.z * 0.125f * 30.0f * system_tick();
	self->angle = vec3_wrap_angle(self->angle);

	// Prevent ship from going past the landing position of a SECTION_JUMP if going backwards.
	if (flags_not(self->flags, SHIP_DIRECTION_FORWARD) && flags_is(self->section->prev->flags, SECTION_JUMP)) {
		vec3_t repulse = vec3_sub(self->section->next->center, self->section->center);
		self->velocity = vec3_add(self->velocity, vec3_mulf(repulse, 2.0f));
	}

	ship_player_update_sfx(self);
}


void ship_player_update_rescue(ship_t *self) {

	section_t *next = self->section->next;

	if (flags_is(self->flags, SHIP_IN_TOW)) {
		self->temp_target = vec3_add(self->temp_target, vec3_mulf(vec3_sub(next->center, self->temp_target), 0.0078125f)); // / 128
/*                 target.vx += (nextSection->centre.vx - target.vx)>>7;
                target.vy += (nextSection->centre.vy - target.vy)>>7;
                target.vz += (nextSection->centre.vz - target.vz)>>7; */

		self->velocity = vec3_sub(self->temp_target, self->position);
/*                 playerShip->vpivot.vx = ((target.vx - playerShip->ppivot.vx)*60)/FR60;
                playerShip->vpivot.vy = ((target.vy - playerShip->ppivot.vy)*60)/FR60;
                playerShip->vpivot.vz = ((target.vz - playerShip->ppivot.vz)*60)/FR60; */

		vec3_t target_dir = vec3_sub(next->center, self->section->center);
/*                 targetVector.vx = nextSection->centre.vx - playerShip->nearTrkSect->centre.vx;
                targetVector.vy = nextSection->centre.vy - playerShip->nearTrkSect->centre.vy;
                targetVector.vz = nextSection->centre.vz - playerShip->nearTrkSect->centre.vz;

                playerShip->vhdg = -ratan2(targetVector.vx, targetVector.vz) - playerShip->hdg ;
                if(playerShip->vhdg > 2048) playerShip->vhdg = playerShip->vhdg - 4096;
                else if(playerShip->vhdg < -2048) playerShip->vhdg = playerShip->vhdg + 4096;
                playerShip->hdg += playerShip->vhdg>>6;
                playerShip->hdg   = ang(playerShip->hdg);
 */

		self->angular_velocity.y = wrap_angle(-bump_atan2f(target_dir.x, target_dir.z) - self->angle.y) * 0.46875f;//1.875f;// * (1.0/16.0) * 30;
		// vpivot y
		self->angle.y = wrap_angle(self->angle.y + self->angular_velocity.y * system_tick());
	}

	// angle x->pitch, y->hdg, z->roll
	// angvel x->vpitch, y->vhdg, z->vroll

	// vpivotx
	self->angle.x -= self->angle.x * 0.125f * 30.0f * system_tick(); // pitch -= pitch / 8
	// vpivotz
	self->angle.z -= self->angle.z * 0.03125f * 30.0f * system_tick(); // roll -= roll / 32

/*         playerShip->pitch -= playerShip->pitch>>3;
        playerShip->roll -= playerShip->roll>>5; */

	self->velocity = vec3_sub(self->velocity, vec3_mulf(self->velocity, 0.0625f * 30.0f * system_tick())); // (1/16) * (tick * 30)
	self->position = vec3_add(self->position, vec3_mulf(self->velocity, 0.03125f * 30.0f * system_tick())); // (1/32) * (tick * 30)

/*         playerShip->vpivot.vx -= playerShip->vpivot.vx>>4;
        playerShip->vpivot.vy -= playerShip->vpivot.vy>>4;
        playerShip->vpivot.vz -= playerShip->vpivot.vz>>4;

        playerShip->ppivot.vx += playerShip->vpivot.vx>>5;
        playerShip->ppivot.vy += playerShip->vpivot.vy>>5;
        playerShip->ppivot.vz += playerShip->vpivot.vz>>5; */

	// Are we done being rescued?
	float distance = vec3_len(vec3_sub(self->position, self->temp_target));
	if (flags_is(self->flags, SHIP_IN_TOW) && distance < 800.0f) {
		self->update_func = ship_player_update_race;
		self->update_timer = 0.0f;
		flags_rm(self->flags, SHIP_IN_RESCUE);
		flags_rm(self->flags, SHIP_VIEW_REMOTE);

		if (flags_is(self->flags, SHIP_VIEW_INTERNAL)) {
			g.camera.update_func = camera_update_race_internal;
		}
		else {
			g.camera.update_func = camera_update_race_external;
		}
	}
}

ship_t *ship_player_find_target(ship_t *self) {
	int shortest_distance = 256;
	ship_t *nearest_ship = NULL;

	for (int i = 0; i < len(g.ships); i++) {
		ship_t *other = &g.ships[i];
		if (self == other) {
			continue;
		}
		
		// We are on a branch
		if (flags_is(self->section->flags, SECTION_JUNCTION)) {
			// Other ship is on same branch
			if (other->section->flags & SECTION_JUNCTION) {
				int distance = other->section->num - self->section->num;

				if (distance < shortest_distance && distance > 0) {
					shortest_distance = distance;
					nearest_ship = other;
				}
			}

			// Other ship is not on branch
			else {
				section_t *section = self->section;
				for (int distance = 0; distance < 10; distance++) {
					section = section->next;
					if (other->section == section && distance < shortest_distance && distance > 0) {
						shortest_distance = distance;
						nearest_ship = other;
						break;
					}
				}
			}
		}

		// We are not on a branch
		else {
			// Other ship is on a branch - check if we can reach the other ship's section
			if (flags_is(other->section->flags, SECTION_JUNCTION)) {
				section_t *section = self->section;
				for (int distance = 0; distance < 10; distance++) {
					if (section->junction) {
						section = section->junction;
					}
					else {
						section = section->next;
					}
					if (other->section == section && distance < shortest_distance && distance > 0) {
						shortest_distance = distance;
						nearest_ship = other;
						break;
					}
				}
			}

			// Other ship is not on a branch
			else {
				int distance = other->section->num - self->section->num;

				if (distance < shortest_distance && distance > 0) {
					shortest_distance = distance;
					nearest_ship = other;
				}
			}
		}
	}

	if (shortest_distance < 10) {
		return nearest_ship;
	}
	else {
		return NULL;
	}
}

