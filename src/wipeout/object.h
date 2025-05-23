#ifndef OBJECT_H
#define OBJECT_H

#include "../types.h"
#include "../render.h"
#include "../utils.h"
#include "image.h"

// Primitive Structure Stub ( Structure varies with primitive type )

typedef struct Primitive {
	int16_t type; // Type of Primitive
	int16_t flag;
} Primitive;


typedef struct F3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t pad1;
	uint32_t color;
} F3;

typedef struct FT3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	int16_t pad1;
	uint32_t color;
	float u[3];
	float v[3];
} FT3;

typedef struct F4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	uint32_t color;
} F4;

typedef struct FT4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint8_t u3;
	uint8_t v3;
	int16_t pad1;
	uint32_t color;
	float u[4];
	float v[4];
} FT4;

typedef struct G3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t pad1;
	uint32_t color[3];
} G3;

typedef struct GT3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	int16_t pad1;
	uint32_t color[3];
	float u[3];
	float v[3];
} GT3;

typedef struct G4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	uint32_t color[4];
} G4;

typedef struct GT4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint8_t u3;
	uint8_t v3;
	int16_t pad1;
	uint32_t color[4];
	float u[4];
	float v[4];
} GT4;




/* LIGHT SOURCED POLYGONS
*/

typedef struct LSF3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t normal; // Indices of the normals
	uint32_t color;
} LSF3;

typedef struct LSFT3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t normal; // Indices of the normals
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint32_t color;
} LSFT3;

typedef struct LSF4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t normal; // Indices of the normals
	int16_t pad1;
	uint32_t color;
} LSF4;

typedef struct LSFT4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t normal; // Indices of the normals
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint8_t u3;
	uint8_t v3;
	uint32_t color;
} LSFT4;

typedef struct LSG3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t normals[3]; // Indices of the normals
	uint32_t color[3];
} LSG3;

typedef struct LSGT3 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[3]; // Indices of the coords
	int16_t normals[3]; // Indices of the normals
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint32_t color[3];
} LSGT3;

typedef struct LSG4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t normals[4]; // Indices of the normals
	uint32_t color[4];
} LSG4;

typedef struct LSGT4 {
	int16_t type; // Type of primitive
	int16_t flag;
	int16_t coords[4]; // Indices of the coords
	int16_t normals[4]; // Indices of the normals
	int16_t texture;
	int16_t cba;
	int16_t tsb;
	uint8_t u0;
	uint8_t v0;
	uint8_t u1;
	uint8_t v1;
	uint8_t u2;
	uint8_t v2;
	uint8_t u3;
	uint8_t v3;
	int16_t pad1;
	uint32_t color[4];
} LSGT4;






/* OTHER PRIMITIVE TYPES
*/
typedef struct SPR {
	int16_t type;
	int16_t flag;
	int16_t coord;
	int16_t width;
	int16_t height;
	int16_t texture;
	uint32_t color;
} SPR;


typedef struct Spline {
	int16_t type; // Type of primitive
	int16_t flag;
	vec3_t control1;
	vec3_t position;
	vec3_t control2;
	uint32_t color;
} Spline;


typedef struct PointLight {
	int16_t type;
	int16_t flag;
	vec3_t position;
	uint32_t color;
	int16_t startFalloff;
	int16_t endFalloff;
} PointLight;


typedef struct SpotLight {
	int16_t type;
	int16_t flag;
	vec3_t position;
	vec3_t direction;
	uint32_t color;
	int16_t startFalloff;
	int16_t endFalloff;
	int16_t coneAngle;
	int16_t spreadAngle;
} SpotLight;


typedef struct InfiniteLight {
	int16_t type;
	int16_t flag;
	vec3_t direction;
	uint32_t color;
} InfiniteLight;






// PRIMITIVE FLAGS

#define PRM_SINGLE_SIDED 0x0001
#define PRM_SHIP_ENGINE  0x0002
#define PRM_TRANSLUCENT  0x0004



#define PRM_TYPE_F3               1
#define PRM_TYPE_FT3              2
#define PRM_TYPE_F4               3
#define PRM_TYPE_FT4              4
#define PRM_TYPE_G3               5
#define PRM_TYPE_GT3              6
#define PRM_TYPE_G4               7
#define PRM_TYPE_GT4              8

#define PRM_TYPE_LF2              9
#define PRM_TYPE_TSPR             10
#define PRM_TYPE_BSPR             11

#define PRM_TYPE_LSF3             12
#define PRM_TYPE_LSFT3            13
#define PRM_TYPE_LSF4             14
#define PRM_TYPE_LSFT4            15
#define PRM_TYPE_LSG3             16
#define PRM_TYPE_LSGT3            17
#define PRM_TYPE_LSG4             18
#define PRM_TYPE_LSGT4            19

#define PRM_TYPE_SPLINE           20

#define PRM_TYPE_INFINITE_LIGHT    21
#define PRM_TYPE_POINT_LIGHT       22
#define PRM_TYPE_SPOT_LIGHT        23

typedef enum {
	OBJNAME_START,
	OBJNAME_REDL,
	OBJNAME_DONKEY,
	OBJNAME_LOSTAD,
	OBJNAME_STAD_,
	OBJNAME_NEWSTAD_,
	OBJNAME_ERROR
} object_name;


typedef struct Object {
	mat4_t *mat;
	int16_t vertices_len; // Number of Vertices
	vector_t *vertices; // Pointer to 3D Points

	int16_t normals_len; // Number of Normals
	vec3_t *normals; // Pointer to 3D Normals

	int16_t primitives_len; // Number of Primitives
	Primitive *primitives; // Pointer to Z Sort Primitives

	vec3_t origin;
	int32_t extent;	// I hijacked this for a name enum
			// was: Flags for object characteristics
	int16_t flags; // Next object in list
	float radius;
	struct Object *next; // Next object in list

	vector_t *xform; // storage for transformed object verts
}  Object;

typedef union Prm {
	uint8_t *ptr;
	int16_t *sptr;
	int32_t *lptr;
	Object *object;
	Primitive        *primitive;

	F3               *f3;
	FT3              *ft3;
	F4               *f4;
	FT4              *ft4;
	G3               *g3;
	GT3              *gt3;
	G4               *g4;
	GT4              *gt4;
	SPR              *spr;
	Spline           *spline;
	PointLight       *pointLight;
	SpotLight        *spotLight;
	InfiniteLight    *infiniteLight;

	LSF3             *lsf3;
	LSFT3            *lsft3;
	LSF4             *lsf4;
	LSFT4            *lsft4;
	LSG3             *lsg3;
	LSGT3            *lsgt3;
	LSG4             *lsg4;
	LSGT4            *lsgt4;
} Prm;

Object *objects_load(char *name, texture_list_t tl);
void object_draw(Object *object, mat4_t *mat);

#endif
