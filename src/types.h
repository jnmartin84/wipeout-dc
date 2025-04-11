#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <kos.h>
typedef struct rgba_t {
	uint8_t r, g, b, a;
} rgba_t;

typedef struct {
	float x, y;
} vec2_t;


typedef struct {
	int32_t x, y;
} vec2i_t;


typedef struct {
	float x, y, z;
} vec3_t;

typedef union {
	float m[16];
	float cols[4][4];
} mat4_t;

typedef struct {
	vec3_t pos;
	vec2_t uv;
	rgba_t color;
	int spec;
} vertex_t;

typedef struct {
	pvr_vertex_t vertices[3];
} tris_t;

#define rgba(R, G, B, A) ((rgba_t){.r = R, .g = G, .b = B, .a = A})
#define vec2(X, Y) ((vec2_t){.x = X, .y = Y})
#define vec3(X, Y, Z) ((vec3_t){.x = X, .y = Y, .z = Z})
#define vec2i(X, Y) ((vec2i_t){.x = X, .y = Y})

#define mat4(m0,m1,m2,m3,m4,m5,m6,m7,m8,m9,m10,m11,m12,m13,m14,m15) \
	(mat4_t){.cols = { \
		{m0,   m1,  m2,  m3}, \
		{m4,   m5,  m6,  m7}, \
		{m8,   m9, m10, m11}, \
		{m12, m13, m14, m15}  \
	}}

#define mat4_identity() mat4( \
		1, 0, 0, 0, \
		0, 1, 0, 0, \
		0, 0, 1, 0, \
		0, 0, 0, 1 \
	)

// branch-free, division-free atan2f approximation
// copysignf has a branch
#define quarterpi_i754 0.785398185253143310546875f
#define halfpi_i754 1.57079637050628662109375f
#define pi_i754 3.1415927410125732421875f
#define twopi_i754 6.283185482025146484375f
#define recip2pi 0.1591549386630056853734159179072f
#define recip4k 0.000244140625f
#define f8over4k 0.001953125f
#define approx_recip(x) (1.0f / sqrtf((x)*(x)))
static inline float bump_atan2f(const float y, const float x)
{
	float abs_y = fabsf(y) + 1e-10f;
	float absy_plus_absx = abs_y + fabsf(x);
	float inv_absy_plus_absx = approx_recip(absy_plus_absx);
	float angle = halfpi_i754 - copysignf(quarterpi_i754, x);
	float r = (x - copysignf(abs_y, x)) * inv_absy_plus_absx;
	angle += (0.1963f * r * r - 0.9817f) * r;
	return copysignf(angle, y);
}

static inline vec2_t vec2_mulf(vec2_t a, float f) {
	return vec2(
		a.x * f,
		a.y * f
	);
}

static inline vec2i_t vec2i_mulf(vec2i_t a, float f) {
	return vec2i(
		a.x * f,
		a.y * f
	);
}


static inline vec3_t vec3_add(vec3_t a, vec3_t b) {
	return vec3(
		a.x + b.x,
		a.y + b.y,
		a.z + b.z
	);
}

static inline vec3_t vec3_sub(vec3_t a, vec3_t b) {
	return vec3(
		a.x - b.x,
		a.y - b.y,
		a.z - b.z
	);
}

static inline vec3_t vec3_mul(vec3_t a, vec3_t b) {
	return vec3(
		a.x * b.x,
		a.y * b.y,
		a.z * b.z
	);
}

static inline vec3_t vec3_mulf(vec3_t a, float f) {
	return vec3(
		a.x * f,
		a.y * f,
		a.z * f
	);
}

static inline vec3_t vec3_inv(vec3_t a) {
	return vec3(-a.x, -a.y, -a.z);
}

// this is never called with negative , i checked
static inline vec3_t vec3_divf(vec3_t a, float f) {
	float rf = approx_recip(f);
	return vec3(
		a.x * rf,
		a.y * rf,
		a.z * rf
	);
}

static inline float vec3_len(vec3_t a) {
	float len;
	vec3f_length(a.x, a.y, a.z, len);
	return len;
}

extern mat4_t __attribute__((aligned(32))) cross_mat;
extern mat4_t __attribute__((aligned(32))) store_mat;

#include <kos.h>

static inline vec3_t vec3_cross(vec3_t a, vec3_t b) {
 	return vec3(
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	);
}

static inline float vec3_dot(vec3_t a, vec3_t b) {
	//return a.x * b.x + a.y * b.y + a.z * b.z;
	float dot;
	vec3f_dot(a.x, a.y, a.z, b.x, b.y, b.z, dot);
	return dot;
}

static inline vec3_t vec3_lerp(vec3_t a, vec3_t b, float t) {
	return vec3(
		a.x + t * (b.x - a.x),
		a.y + t * (b.y - a.y),
		a.z + t * (b.z - a.z)
	);
}

static inline vec3_t vec3_normalize(vec3_t a) {
	// this is only used in one place and it is ok to modify
	vec3f_normalize(a.x,a.y,a.z);
	return a;
}

static inline float wrap_angle(float a) {
	a = fmodf(a + F_PI, twopi_i754);

	if (a < 0) {
		a += twopi_i754;
	}

	return a - F_PI;
}

uint32_t argb_from_u32(uint32_t v);
rgba_t __attribute__((noinline)) rgba_from_u32(uint32_t v);
float vec3_angle(vec3_t a, vec3_t b);
vec3_t vec3_wrap_angle(vec3_t a);
vec3_t vec3_normalize(vec3_t a);
vec3_t vec3_project_to_ray(vec3_t p, vec3_t r0, vec3_t r1);
float vec3_distance_to_plane(vec3_t p, vec3_t plane_pos, vec3_t plane_normal);
vec3_t vec3_reflect(vec3_t incidence, vec3_t normal, float f);

float wrap_angle(float a);

vec3_t vector_transform(vector_t a);
vec3_t vec3_transform(vec3_t a, mat4_t *mat);
void mat4_set_translation(mat4_t *mat, vec3_t pos);
void mat4_set_yaw_pitch_roll(mat4_t *m, vec3_t rot);
void mat4_set_roll_pitch_yaw(mat4_t *mat, vec3_t rot);
void mat4_translate(mat4_t *mat, vec3_t translation);
void mat4_mul(mat4_t *res, mat4_t *a, mat4_t *b);

#endif
