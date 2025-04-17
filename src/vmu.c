#include <kos.h>
#include <dc/vmu_fb.h>
#include <dc/vmu_pkg.h>

#include "vmudata.h"

int32_t ControllerPakStatus = 1;
int32_t Pak_Memory = 0;

static char full_fn[20];

char *get_vmu_fn(maple_device_t *vmudev, char *fn) {
	if (fn)
		sprintf(full_fn, "/vmu/%c%d/%s", 'a'+vmudev->port, vmudev->unit, fn);
	else
		sprintf(full_fn, "/vmu/%c%d", 'a'+vmudev->port, vmudev->unit);

	return full_fn;
}

#include "owl.h"

// do all of the swapping needed to draw with vmu_draw_lcd
void fix_xbm(unsigned char *p)
{
    unsigned char tmp[6*32];
	for (int i = 31; i > -1; i--) {
		memcpy(&tmp[(31 - i) * 6], &p[i * 6], 6);
	}

	memcpy(p, tmp, 6 * 32);

	for (int j = 0; j < 32; j++) {
		for (int i = 0; i < 6; i++) {
			uint8_t tmpb = p[(j * 6) + (5 - i)];
			tmp[(j * 6) + i] = tmpb;
		}
	}

	memcpy(p, tmp, 6 * 32);
}

void draw_vmu_icon(void) {
	maple_device_t *vmudev = NULL;

	fix_xbm(owl2_bits);

	// draw on the first vmu screen found
	if ((vmudev = maple_enum_type(0, MAPLE_FUNC_LCD)))
		vmu_draw_lcd(vmudev, owl2_bits);
}