#include <math.h>
#include <kos.h>

#include "types.h"
#include "utils.h"

// lifted HSV scaling from Doom 64, thanks
#define recip60 0.01666666753590106964111328125f
#define MAXINT ((int)0x7fffffff) /* max pos 32-bit int */
#define MININT ((int)0x80000000) /* max negative 32-bit integer */
#define recip255 0.0039215688593685626983642578125f

static uint32_t LightGetHSV(uint8_t r, uint8_t g, uint8_t b) {
	uint8_t h_, s_, v_;
	int min;
	int max;
	float deltamin;
	float deltamax;
	float j;
	float x = 0;
	float xr;
	float xg;
	float xb;
	float sum = 0;

	max = MAXINT;

	if (r < max)
		max = r;
	if (g < max)
		max = g;
	if (b < max)
		max = b;

	min = MININT;

	if (r > min)
		min = r;
	if (g > min)
		min = g;
	if (b > min)
		min = b;

	deltamin = (float)min * recip255;
	deltamax = deltamin - ((float)max * recip255);

	if (deltamax == 0.0f)
		deltamax = 1e-10f;

	float recip_deltamax = 1.0f / deltamax;

	if (deltamin == 0.0f)
		j = 0.0f;
	else
		j = deltamax / deltamin;

	if (j != 0.0f) {
		xr = (float)r * recip255;
		xg = (float)g * recip255;
		xb = (float)b * recip255;

		if (xr != deltamin) {
			if (xg != deltamin) {
				if (xb == deltamin) {
					sum = ((deltamin - xg) * recip_deltamax + 4.0f) - ((deltamin - xr) * recip_deltamax);
				}
			} else {
				sum = ((deltamin - xr) * recip_deltamax + 2.0f) - ((deltamin - xb) * recip_deltamax);
			}
		} else {
			sum = ((deltamin - xb) * recip_deltamax) - ((deltamin - xg) * recip_deltamax);
		}

		x = (sum * 60.0f);

		if (x < 0.0f)
			x += 360.0f;
		else if (x > 360.0f)
			x -= 360.0f;
	} else {
		j = 0.0f;
	}

	h_ = (uint8_t)(x * 0.708333313465118408203125f); // x / 360 * 255
	s_ = (uint8_t)(j * 255.0f);
	v_ = (uint8_t)(deltamin * 255.0f);

	return (((h_ & 0xff) << 16) | ((s_ & 0xff) << 8) | (v_ & 0xff));
}

/*
===================
=
= LightGetRGB
= Set RGB values based on given HSV
=
===================
*/

static uint32_t LightGetRGB(uint8_t h, uint8_t s, uint8_t v) {
	uint8_t r, g, b;

	float x;
	float j;
	float i;
	float t;
	int table;
	float xr = 0;
	float xg = 0;
	float xb = 0;

	j = (float)h * 1.41176474094390869140625f; // h / 255 * 360

	if (j < 0.0f)
		j += 360.0f;
	else if (j > 360.0f)
		j -= 360.0f;

	x = (float)s * recip255;
	i = (float)v * recip255;

	if (x != 0.0f) {
		table = (int)(j * recip60);
		if (table < 6) {
			t = j * recip60;

			switch (table) {
			case 0:
				xr = i;
				xg = (1.0f - ((1.0f - (t - (float)table)) * x)) * i;
				xb = (1.0f - x) * i;
				break;
			case 1:
				xr = (1.0f - (x * (t - (float)table))) * i;
				xg = i;
				xb = (1.0f - x) * i;
				break;
			case 2:
				xr = (1.0f - x) * i;
				xg = i;
				xb = (1.0f - ((1.0f - (t - (float)table)) * x)) * i;
				break;
			case 3:
				xr = (1.0f - x) * i;
				xg = (1.0f - (x * (t - (float)table))) * i;
				xb = i;
				break;
			case 4:
				xr = (1.0f - ((1.0f - (t - (float)table)) * x)) * i;
				xg = (1.0f - x) * i;
				xb = i;
				break;
			case 5:
				xr = i;
				xg = (1.0f - x) * i;
				xb = (1.0f - (x * (t - (float)table))) * i;
				break;
			}
		}
	} else {
		xr = xg = xb = i;
	}

	r = (uint8_t)(xr * 255.0f);
	g = (uint8_t)(xg * 255.0f);
	b = (uint8_t)(xb * 255.0f);

	return (((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff));
}

uint32_t argb_from_u32_alpha(uint32_t v, uint8_t a) {
	float l_flt;
	int factor;
	int h, s, _v;
	int hsv;
	uint8_t vr,vg,vb;

	vr = ((v >> 24) & 0xff);
	vg = ((v >> 16) & 0xff);
	vb = ((v >> 8) & 0xff);

 	hsv = LightGetHSV(vr, vg, vb);

	h = (hsv >> 16) & 0xFF;
	s = (hsv >> 8) & 0xFF;
	_v = hsv & 0xFF;

	factor = _v;

	l_flt = (float)factor * 2.2f;

	_v = (int)l_flt;

	if (_v < 0)
		_v = 0;
	if (_v > 255)
		_v = 255;

	int rgb = LightGetRGB(h, s, _v);
	return (a << 24) | (rgb & 0x00ffffff);
}

uint32_t notex_argb_from_u32(uint32_t v) {
	return 0xff000000 | ((v >> 8)&0x00ffffff);
}

uint32_t argb_from_u32(uint32_t v) {
	float l_flt;
	int factor;
	int h, s, _v;
	int hsv;
	uint8_t vr,vg,vb;

	vr = ((v >> 24) & 0xff);
	vg = ((v >> 16) & 0xff);
	vb = ((v >> 8) & 0xff);

 	hsv = LightGetHSV(vr, vg, vb);

	h = (hsv >> 16) & 0xFF;
	s = (hsv >> 8) & 0xFF;
	_v = hsv & 0xFF;

	factor = _v;

	l_flt = (float)factor * 2.2f;

	_v = (int)l_flt;

	if (_v < 0)
		_v = 0;
	if (_v > 255)
		_v = 255;

	int rgb = LightGetRGB(h, s, _v);
	return 0xff000000 | (rgb & 0x00ffffff);
}

uint32_t eng_argb_from_u32(uint32_t v) {
	float l_flt;
	int factor;
	int h, s, _v;
	int hsv;
	uint8_t vr,vg,vb;

	vr = ((v >> 24) & 0xff) * 4;
	vg = ((v >> 16) & 0xff) * 4;
	vb = ((v >> 8) & 0xff) * 4;

 	hsv = LightGetHSV(vr, vg, vb);

	h = (hsv >> 16) & 0xFF;
	s = (hsv >> 8) & 0xFF;
	_v = hsv & 0xFF;

	factor = _v;

	l_flt = (float)factor * 2.2f;

	_v = (int)l_flt;

	if (_v < 0)
		_v = 0;
	if (_v > 255)
		_v = 255;

	int rgb = LightGetRGB(h, s, _v);
	return 0xff000000 | (rgb & 0x00ffffff);
}

uint32_t argb_from_u32_usealpha(uint32_t v) {
	float l_flt;
	int factor;
	int h, s, _v;
	int hsv;
	uint8_t vr,vg,vb;

	vr = ((v >> 24) & 0xff);
	vg = ((v >> 16) & 0xff);
	vb = ((v >> 8) & 0xff);

 	hsv = LightGetHSV(vr, vg, vb);

	h = (hsv >> 16) & 0xFF;
	s = (hsv >> 8) & 0xFF;
	_v = hsv & 0xFF;

	factor = _v;

	l_flt = (float)factor * 2.2f;

	_v = (int)l_flt;

	if (_v < 0)
		_v = 0;
	if (_v > 255)
		_v = 255;

	int rgb = LightGetRGB(h, s, _v);
	return (v << 24) | (rgb & 0x00ffffff);
}

vec3_t vec3_wrap_angle(vec3_t a) {
	return vec3(wrap_angle(a.x), wrap_angle(a.y), wrap_angle(a.z));
}

float vec3_angle(vec3_t a, vec3_t b) {
	float dot = vec_dot(a,b);

	float lena = vec_length(a);

	float lenb = vec_length(b);

	float magnitude = lena * lenb;

	float cosine = (magnitude < 0.001f)
		? 1
		: dot * approx_recip(magnitude);

	return acosf(cosine);
}

// this gets used to resolve ship-ship collisions
// matrix is pre-loaded because it gets used over multiple calls
vec3_t vector_transform(vector_t a) {
	float rx = a.x;
	float ry = a.y;
	float rz = a.z;

	mat_trans_single3(rx,ry,rz);

	return vec3(rx,ry,rz);
}

vec3_t vec3_transform(vec3_t a) {
	float rx = a.x;
	float ry = a.y;
	float rz = a.z;

	mat_trans_single3(rx,ry,rz);

	return vec3(rx,ry,rz);
}

vec3_t vec3_project_to_ray(vec3_t p, vec3_t r0, vec3_t r1) {
	vec3_t ray = vec3_normalize(vec3_sub(r1, r0));
	float dp = vec3_dot(vec3_sub(p, r0), ray);
	return vec3_add(r0, vec3_mulf(ray, dp));
}

float vec3_distance_to_plane(vec3_t p, vec3_t plane_pos, vec3_t plane_normal) {
	float dot_product = vec3_dot(vec3_sub(plane_pos, p), plane_normal);
	float norm_dot_product = -vec_fipr(plane_normal);
	float rndp = copysignf(approx_recip(norm_dot_product), norm_dot_product);
	return dot_product * rndp;
}

vec3_t vec3_reflect(vec3_t incidence, vec3_t normal) {
	return vec3_add(incidence, vec3_mulf(normal, vec3_dot(normal, vec3_mulf(incidence, -2.0)))); 
}

void mat4_set_translation(mat4_t *mat, vec3_t pos) {
	mat->cols[3][0] = pos.x;
	mat->cols[3][1] = pos.y;
	mat->cols[3][2] = pos.z;
}

void mat4_set_yaw_pitch_roll(mat4_t *mat, vec3_t rot) {
	float sx = sinf( rot.x);
	float cx = cosf( rot.x);
	float sy = sinf(-rot.y);
	float cy = cosf(-rot.y);
	float sz = sinf(-rot.z);
	float cz = cosf(-rot.z);

	mat->cols[0][0] = cy * cz + sx * sy * sz;
	mat->cols[1][0] = cz * sx * sy - cy * sz;
	mat->cols[2][0] = cx * sy;
	mat->cols[0][1] = cx * sz;
	mat->cols[1][1] = cx * cz;
	mat->cols[2][1] = -sx;
	mat->cols[0][2] = -cz * sy + cy * sx * sz;
	mat->cols[1][2] = cy * cz * sx + sy * sz;
	mat->cols[2][2] = cx * cy;
}

void mat4_set_roll_pitch_yaw(mat4_t *mat, vec3_t rot) {
	float sx = sinf( rot.x);
	float cx = cosf( rot.x);
	float sy = sinf(-rot.y);
	float cy = cosf(-rot.y);
	float sz = sinf(-rot.z);
	float cz = cosf(-rot.z);

	mat->cols[0][0] = cy * cz - sx * sy * sz;
	mat->cols[1][0] = -cx * sz;
	mat->cols[2][0] = cz * sy + cy * sx * sz;
	mat->cols[0][1] = cz * sx * sy + cy * sz;
	mat->cols[1][1] = cx *cz;
	mat->cols[2][1] = -cy * cz * sx + sy * sz;
	mat->cols[0][2] = -cx * sy;
	mat->cols[1][2] = sx;
	mat->cols[2][2] = cx * cy;
}

void mat4_translate(mat4_t *mat, vec3_t translation) {
	mat->m[12] = fipr(mat->m[0], mat->m[4], mat->m[8], mat->m[12], translation.x, translation.y, translation.z, 1);
	mat->m[13] = fipr(mat->m[1], mat->m[5], mat->m[9], mat->m[13], translation.x, translation.y, translation.z, 1);
	mat->m[14] = fipr(mat->m[2], mat->m[6], mat->m[10], mat->m[14], translation.x, translation.y, translation.z, 1);
	mat->m[15] = fipr(mat->m[3], mat->m[7], mat->m[11], mat->m[15], translation.x, translation.y, translation.z, 1);
}
