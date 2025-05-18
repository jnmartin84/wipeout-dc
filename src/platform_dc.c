#include "platform.h"
#include "input.h"
#include "system.h"
#include "utils.h"
#include "mem.h"
#include "types.h"

#include "wipeout/game.h"

#include <string.h>
#include <sys/time.h>

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/vmu_fb.h>
#include <dc/vmu_pkg.h>

uint16_t old_buttons = 0, rel_buttons = 0;

extern uint8_t allow_exit;
static volatile int wants_to_exit = 0;
void *gamepad;
char *path_assets = "";
char *path_userdata = "";
char *temp_path = NULL;

void draw_vmu_icon(void);

void platform_exit(void) {
	if (allow_exit)
		wants_to_exit = 1;
}

void *platform_find_gamepad(void) {
	return NULL;
}

int if_to_await = 0;

#define configDeadzone (0x04)

void platform_pump_events()
{
	maple_device_t *cont;
	cont_state_t *state;

	cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
	if (!cont)
		return;
	state = (cont_state_t *)maple_dev_status(cont);

 	rel_buttons = (old_buttons ^ state->buttons);

#if 0
	if (state->ltrig && state->rtrig) {
		char ssfn[256];
		sprintf(ssfn, "/pc/race_%d_%d.pnm", g.race_class, g.circut);
		vid_screen_shot(ssfn);
	}
#endif

	if ((state->buttons & CONT_START) && state->ltrig && state->rtrig)
		platform_exit();

	int last_joyx = state->joyx;
	int last_joyy = state->joyy;

	if (last_joyy == -128)
		last_joyy = -127;

	const uint32_t magnitude_sq = (uint32_t)(last_joyx * last_joyx) + (uint32_t)(last_joyy * last_joyy);

	float stick_x = 0;
	float stick_y = 0;

	if (magnitude_sq > (uint32_t)(configDeadzone * configDeadzone)) {
		stick_x = clamp(((float)last_joyx / 127.0f), -1.0f, 1.0f);
		stick_y = clamp(((float)last_joyy / 127.0f), -1.0f, 1.0f);
	}

	// joystick
	if (stick_x < 0) {
		input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, -stick_x);
		input_set_button_state(INPUT_GAMEPAD_L_STICK_RIGHT, 0);
	} else {
		input_set_button_state(INPUT_GAMEPAD_L_STICK_RIGHT, stick_x);
		input_set_button_state(INPUT_GAMEPAD_L_STICK_LEFT, 0);
	}

	if (stick_y < 0) {
		input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, -stick_y);
		input_set_button_state(INPUT_GAMEPAD_L_STICK_DOWN, 0);
	} else {
		input_set_button_state(INPUT_GAMEPAD_L_STICK_DOWN, stick_y);
		input_set_button_state(INPUT_GAMEPAD_L_STICK_UP, 0);
	}

	// work-around so the remap menu is functional
	if (if_to_await)
		input_set_button_state(INPUT_GAMEPAD_START, (state->buttons & CONT_START) && (rel_buttons & CONT_START) ? 1.0f : 0.0f);
	else
		input_set_button_state(INPUT_GAMEPAD_START, (state->buttons & CONT_START) ? 1.0f : 0.0f);

	// work-around so the remap menu is functional
	if (if_to_await)
		input_set_button_state(INPUT_GAMEPAD_A, (state->buttons & CONT_A) && (rel_buttons & CONT_A) ? 1.0f : 0.0f);
	else
		input_set_button_state(INPUT_GAMEPAD_A, (state->buttons & CONT_A) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_B, (state->buttons & CONT_B) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_X, (state->buttons & CONT_X) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_Y, (state->buttons & CONT_Y) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_L_TRIGGER, ((uint8_t)state->ltrig) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_R_TRIGGER, ((uint8_t)state->rtrig) ? 1.0f : 0.0f);

	input_set_button_state(INPUT_GAMEPAD_DPAD_UP, (state->buttons & CONT_DPAD_UP) ? 1.0f : 0.0f);
	input_set_button_state(INPUT_GAMEPAD_DPAD_DOWN, (state->buttons & CONT_DPAD_DOWN) ? 1.0f : 0.0f);
	input_set_button_state(INPUT_GAMEPAD_DPAD_LEFT, (state->buttons & CONT_DPAD_LEFT) ? 1.0f : 0.0f);
	input_set_button_state(INPUT_GAMEPAD_DPAD_RIGHT, (state->buttons & CONT_DPAD_RIGHT) ? 1.0f : 0.0f);

	old_buttons = state->buttons;
}

static float Sys_FloatTime(void) {
  struct timeval tp;
  struct timezone tzp;
  static int secbase = 0;

  gettimeofday(&tp, &tzp);

#define divisor 0.000001f

  if (!secbase) {
    secbase = tp.tv_sec;
    return tp.tv_usec * divisor;
  }

  return (tp.tv_sec - secbase) + tp.tv_usec * divisor;
}

float platform_now(void) {
	return (float)Sys_FloatTime();
}

bool platform_get_fullscreen(void) {
	return true;
}

void platform_set_fullscreen(bool fullscreen) {
}

char platfn[256];

char *platform_get_fn(const char *name) {
	return strcat(strcpy(platfn, path_assets), name);
}

FILE *platform_open_asset(const char *name, const char *mode) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	return fopen(path, mode);
}

uint8_t *platform_load_asset(const char *name, uint32_t *bytes_read) {
	char *path = strcat(strcpy(temp_path, path_assets), name);
	return file_load(path, bytes_read);
}


char *get_vmu_fn(maple_device_t *vmudev, char *fn);
int vmu_check(void);
extern int32_t ControllerPakStatus;
extern int32_t Pak_Memory;
extern const unsigned short vmu_icon_pal[16];
extern uint8_t icon1_data[512*3];

uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
	ssize_t size;
	maple_device_t *vmudev = NULL;
	uint8_t *data;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) {
		dbgio_printf("platform_load_userdata: could not enum\n");
		*bytes_read = 0;
		return NULL;
	}

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY | O_META);
	if (!d) {
		dbgio_printf("platform_load_userdata: could not fs_open %s\n", get_vmu_fn(vmudev, "wipeout.dat"));
		*bytes_read = 0;
		return NULL;
	}

	size = fs_total(d);
	data = calloc(1, size);

	if (!data) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not calloc data\n");
		return NULL;
	}

	vmu_pkg_t pkg;
	memset(&pkg, 0, sizeof(pkg));
	ssize_t res = fs_read(d, data, size);

	if (res < 0) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not fs_read\n");
		return NULL;
	}
	ssize_t total = res;
	while (total < size) {
		res = fs_read(d, data + total, size - total);
		if (res < 0) {
			fs_close(d);
			*bytes_read = 0;
			dbgio_printf("platform_load_userdata: could not fs_read\n");
			return NULL;
		}
		total += res;
	}

	if (total != size) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: total != size\n");
		return NULL;
	}

	fs_close(d);

	if(vmu_pkg_parse(data, size, &pkg) < 0) {
		free(data);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not vmu_pkg_parse\n");
		return NULL;
	}

	uint8_t *bytes = mem_temp_alloc(pkg.data_len);
	if (!bytes) {
		free(data);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not mem_temp_alloc bytes\n");
		return NULL;
	}

	memcpy(bytes, pkg.data, pkg.data_len);
	ControllerPakStatus = 1;
	free(data);

	*bytes_read = pkg.data_len;

	return bytes;
}

#define USERDATA_BLOCK_COUNT 6

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
	uint8 *pkg_out;
	ssize_t pkg_size;
	maple_device_t *vmudev = NULL;

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) {
		dbgio_printf("platform_store_userdata: could not enum\n");
		return 0;
	}

	vmu_pkg_t pkg;
	memset(&pkg, 0, sizeof(vmu_pkg_t));
	strcpy(pkg.desc_short,"Wipeout userdata");
	strcpy(pkg.desc_long, "Wipeout userdata");
	strcpy(pkg.app_id, "Wipeout");
	pkg.icon_cnt = 3;
	pkg.icon_data = icon1_data;
	pkg.icon_anim_speed = 4;
	memcpy(pkg.icon_pal, vmu_icon_pal, sizeof(vmu_icon_pal));
	pkg.data_len = len;
	pkg.data = bytes;

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY | O_META);
	if (!d) {
		if (Pak_Memory < USERDATA_BLOCK_COUNT){
			dbgio_printf("platform_store_userdata: no wipeout file and not enough space\n");
			return 0;
		}
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDWR | O_CREAT | O_META);
		if (!d) {
			dbgio_printf("platform_store_userdata: cant open wipeout for rdwr|creat\n");
			return 0;
		}
	} else {
		fs_close(d);
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_WRONLY | O_META);
		if (!d) {
			dbgio_printf("platform_store_userdata: could not open file\n");
			return 0;
		}
	}

	vmu_pkg_build(&pkg, &pkg_out, &pkg_size);
	if (!pkg_out || pkg_size <= 0) {
		dbgio_printf("platform_store_userdata: vmu_pkg_build failed\n");
		fs_close(d);
		return 0;
	}

	ssize_t rv = fs_write(d, pkg_out, pkg_size);
	ssize_t total = rv;
	while (total < pkg_size) {
		rv = fs_write(d, pkg_out + total, pkg_size - total);
		if (rv < 0) {
			dbgio_printf("platform_store_userdata: could not fs_write\n");
			fs_close(d);
			return -2;
		}
		total += rv;
	}

	fs_close(d);

	free(pkg_out);

	if (total == pkg_size) {
		ControllerPakStatus = 1;
		return len;
	} else {
	    return 0;
	}
}

#define PLATFORM_WINDOW_FLAGS 0

extern	vec2i_t screen_size;

void platform_video_init(void) {
	; //
}

void platform_video_cleanup(void) {
	; //
}

void platform_prepare_frame(void) {
	; //
}

void platform_end_frame(void) {
	; //
}

vec2i_t platform_screen_size(void) {
	return screen_size;
}

KOS_INIT_FLAGS(INIT_DEFAULT);
int main(int argc, char *argv[]) {
	// Figure out the absolute asset and userdata paths. These may either be
	// supplied at build time through -DPATH_ASSETS=.. and -DPATH_USERDATA=..
	// or received at runtime from SDL. Note that SDL may return NULL for these.
	// We fall back to the current directory (i.e. just "") in this case.
	file_t f = fs_open("/cd/wipeout/common/mine.cmp", O_RDONLY);
	if (f != -1) {
		fs_close(f);
		f = 0;
		path_assets = "/cd";
		path_userdata = "/cd/wipeout";
		allow_exit = 0;
	} else {
		f = fs_open("/pc/wipeout/common/mine.cmp", O_RDONLY);
		if (f != -1) {
			fs_close(f);
			f = 0;
			path_assets = "/pc";
			path_userdata = "/pc/wipeout";
			allow_exit = 1;
		} else {
			printf("CANT FIND ASSETS ON /PC or /CD; TERMINATING!\n");
			exit(-1);
		}
	}

	if (snd_stream_init() < 0)
		exit(-1);

	draw_vmu_icon();

	// Reserve some space for concatenating the asset and userdata paths with
	// local filenames.
	temp_path = mem_bump(max(strlen(path_assets), strlen(path_userdata)) + 64);

	gamepad = platform_find_gamepad();

	platform_video_init();
	system_init();

	while (!wants_to_exit) {
		platform_pump_events();
		platform_prepare_frame();
		system_update();
		platform_end_frame();
	}

	system_cleanup();
	platform_video_cleanup();

	return 0;
}
