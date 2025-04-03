#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "utils.h"
#include "mem.h"

#include "wipeout/game.h"

char temp_path2[64];
char *get_path(const char *dir, const char *file) {
	strcpy(temp_path2, dir);
	strcpy(temp_path2 + strlen(dir), file);
	return temp_path2;
}


bool file_exists(const char *path) {
	struct stat s;
	return (stat(path, &s) == 0);
}

uint8_t *file_load(const char *path, uint32_t *bytes_read) {
	FILE *f = fopen(path, "rb");
	error_if(!f, "Could not open file for reading: %s", path);

	fseek(f, 0, SEEK_END);
	int32_t size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	uint8_t *bytes = mem_temp_alloc(size);
	if (!bytes) {
		fclose(f);
		return NULL;
	}

	*bytes_read = fread(bytes, 1, size, f);
	fclose(f);
	
	error_if(*bytes_read != size, "Could not read file: %s", path);
	return bytes;
}




uint32_t file_store(const char *path, void *bytes, int32_t len) {
#if 0
	FILE *f = fopen(path, "wb");
	error_if(!f, "Could not open file for writing: %s", path);

	if (fwrite(bytes, 1, len, f) != len) {
		die("Could not write file file %s", path);
	}
	
	fclose(f);
	return len;
#endif
return 0;
}

bool str_starts_with(const char *haystack, const char *needle) {
	return (strncmp(haystack, needle, strlen(needle)) == 0);
}

#define RECIP_RAND_MAX 4.656612875245796924105750827168e-10f

static inline float int_to_float(uint32_t random) {
    union { uint32_t u32; float f; } u = { .u32 = random >> 9 | 0x3f800000 };
    return u.f - 1.0;
}

float rand_float(float min, float max) {
	uint32_t rint = rand();
	float rfloat = int_to_float(rint);
	return min + (/* (float)rand() * RECIP_RAND_MAX */rfloat) * (max - min);
}

int32_t rand_int(int32_t min, int32_t max) {
	return min + rand() % (max - min);
}
