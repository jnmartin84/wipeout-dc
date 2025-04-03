#include "platform.h"
#include "input.h"
#include "system.h"
#include "utils.h"
#include "mem.h"
#include "types.h"

#include "wipeout/game.h"

#include <string.h>
#include <sys/time.h>
extern uint8_t allow_exit;
static bool wants_to_exit = false;
void *gamepad;
char *path_assets = "";
char *path_userdata = "";
char *temp_path = NULL;

void draw_vmu_icon(void);

void platform_exit(void) {
	if (allow_exit)
		wants_to_exit = true;
}

void *platform_find_gamepad(void) {
	return NULL;
}


#include <dc/maple.h>
#include <dc/maple/controller.h>
//int ssc = 0;
//char ssfn[256];
void render_textures_dump(const char *path);

int if_to_await = 0;

#define configDeadzone (0x08) // 0x20

uint16_t old_buttons = 0, rel_buttons = 0;

void platform_pump_events()
{
	maple_device_t *cont;
	cont_state_t *state;

	cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
	if (!cont)
		return;
	state = (cont_state_t *)maple_dev_status(cont);

 	rel_buttons = (old_buttons ^ state->buttons);

	if ((state->buttons & CONT_START) && state->ltrig && state->rtrig) {
		platform_exit();
	}

	int last_joyx = state->joyx;
	int last_joyy = state->joyy;

	if (last_joyy == -128)
		last_joyy = -127;

	const uint32_t magnitude_sq = (uint32_t)(last_joyx * last_joyx) + (uint32_t)(last_joyy * last_joyy);

	float stick_x = 0;
	float stick_y = 0;

	if (magnitude_sq > (uint32_t)(configDeadzone * configDeadzone)) {
		stick_x = clamp(((float)last_joyx / 127.0f) * 2.0f, -1.0f, 1.0f);
		stick_y = clamp(((float)last_joyy / 127.0f) * 2.0f, -1.0f, 1.0f);
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

	// when if_to_await is true, A and START get debounced for controller remapping menu only
	if (if_to_await)
		input_set_button_state(INPUT_GAMEPAD_START, (state->buttons & CONT_START) && (rel_buttons & CONT_START) ? 1.0f : 0.0f);
	else
		input_set_button_state(INPUT_GAMEPAD_START, (state->buttons & CONT_START) ? 1.0f : 0.0f);

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

float Sys_FloatTime(void) {
  struct timeval tp;
  struct timezone tzp;
  static int secbase;

  gettimeofday(&tp, &tzp);

#define divisor (1 / 1000000.0f)

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


#include <kos.h>
#include <dc/vmu_fb.h>
#include <dc/vmu_pkg.h>

char *get_vmu_fn(maple_device_t *vmudev, char *fn);
int vmu_check(void);
extern int32_t ControllerPakStatus;
extern int32_t Pak_Memory;
extern const unsigned short vmu_icon_pal[16];
extern const uint8_t icon1_data[512*3];

extern void wav_volume(int vol);

uint8_t *platform_load_userdata(const char *name, uint32_t *bytes_read) {
//	vmu_check();
//	if (!ControllerPakStatus) {
//		*bytes_read = 0;
//		return NULL;
//	}

	ssize_t size;
	maple_device_t *vmudev = NULL;
	uint8_t *data;

	ControllerPakStatus = 0;

//	wav_volume(0);
//	for (int i=0;i<1024;i++) {
//		;
//	}

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) {
		dbgio_printf("platform_load_userdata: could not enum\n");
		*bytes_read = 0;
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY);
	if (!d) {
		dbgio_printf("platform_load_userdata: could not fs_open %s\n", get_vmu_fn(vmudev, "wipeout.dat"));
		*bytes_read = 0;
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	size = fs_total(d);
	data = calloc(1, size);

	if (!data) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not calloc data\n");
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	vmu_pkg_t pkg;
	memset(&pkg, 0, sizeof(pkg));
	ssize_t res = fs_read(d, data, size);

	if (res < 0) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not fs_read\n");
//		wav_volume(224 * save.music_volume);
		return NULL;
	}
	ssize_t total = res;
	while (total < size) {
		res = fs_read(d, data + total, size - total);
		if (res < 0) {
			fs_close(d);
			*bytes_read = 0;
			dbgio_printf("platform_load_userdata: could not fs_read\n");
//			wav_volume(224 * save.music_volume);
			return NULL;
		}
		total += res;
	}

	if (total != size) {
		fs_close(d);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: total != size\n");
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	fs_close(d);

	if(vmu_pkg_parse(data, &pkg) < 0) {
		free(data);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not vmu_pkg_parse\n");
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	uint8_t *bytes = mem_temp_alloc(pkg.data_len);
	if (!bytes) {
		free(data);
 		*bytes_read = 0;
		dbgio_printf("platform_load_userdata: could not mem_temp_alloc bytes\n");
//		wav_volume(224 * save.music_volume);
		return NULL;
	}

	memcpy(bytes, pkg.data, pkg.data_len);
	ControllerPakStatus = 1;
	free(data);

	*bytes_read = pkg.data_len;

//	wav_volume(224 * save.music_volume);

	return bytes;
}
#define USERDATA_BLOCK_COUNT 6

uint32_t platform_store_userdata(const char *name, void *bytes, int32_t len) {
	uint8 *pkg_out;
	ssize_t pkg_size;
	maple_device_t *vmudev = NULL;

//	wav_volume(0);
//	for (int i=0;i<1024;i++) {
//		;
//	}

	//vmu_check();
	//if (!ControllerPakStatus) {
//		wav_volume(224 * save.music_volume);
//		dbgio_printf("platform_load_userdata: could not mem_temp_alloc bytes\n");
//		return 0;
//	}

	ControllerPakStatus = 0;

	vmudev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!vmudev) {
		dbgio_printf("platform_store_userdata: could not enum\n");
//		wav_volume(224 * save.music_volume);
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

	file_t d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDONLY);
	if (!d) {
		if (Pak_Memory < USERDATA_BLOCK_COUNT){
			dbgio_printf("platform_store_userdata: no wipeout file and not enough space\n");
			wav_volume(224 * save.music_volume);
			return 0;
		}
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_RDWR | O_CREAT);
		if (!d) {
			dbgio_printf("platform_store_userdata: cant open wipeout for rdwr|creat\n");			
//			wav_volume(224 * save.music_volume);
			return 0;
		}
	} else {
		fs_close(d);
		d = fs_open(get_vmu_fn(vmudev, "wipeout.dat"), O_WRONLY);
		if (!d) {
			dbgio_printf("platform_store_userdata: could not open file\n");			
//			wav_volume(224 * save.music_volume);
			return 0;
		}
	}

	vmu_pkg_build(&pkg, &pkg_out, &pkg_size);
	if (!pkg_out || pkg_size <= 0) {
		dbgio_printf("platform_store_userdata: vmu_pkg_build failed\n");		
//		wav_volume(224 * save.music_volume);
		fs_close(d);
		return 0;
	}

	ssize_t rv = fs_write(d, pkg_out, pkg_size);
	ssize_t total = rv;
	while (total < pkg_size) {
		rv = fs_write(d, pkg_out + total, pkg_size - total);
		if (rv < 0) {
			dbgio_printf("platform_store_userdata: could not fs_write\n");
//			wav_volume(224 * save.music_volume);
			fs_close(d);
			return -2;
		}
		total += rv;
	}

	fs_close(d);

	free(pkg_out);

//	wav_volume(224 * save.music_volume);

	if (total == pkg_size) {
		ControllerPakStatus = 1;
		return len;
	} else {
	    return 0;
	}
}

	#define PLATFORM_WINDOW_FLAGS 0
	static vec2i_t screen_size = vec2i(0, 0);

	void platform_video_init(void) {
	}

	void platform_video_cleanup(void) {
	}

	void platform_prepare_frame(void) {
	}

	void platform_end_frame(void) {
	}

	rgba_t *platform_get_screenbuffer(int32_t *pitch) {
		return NULL;
	}

	vec2i_t platform_screen_size(void) {
		screen_size = vec2i(640,480);
		return screen_size;
	}

#include <kos.h>

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

	// Load gamecontrollerdb.txt if present.
	// FIXME: Should this load from userdata instead?
//	char *gcdb_path = strcat(strcpy(temp_path, path_assets), "gamecontrollerdb.txt");
//	int gcdb_res = SDL_GameControllerAddMappingsFromFile(gcdb_path);
//	if (gcdb_res < 0) {
//		printf("Failed to load gamecontrollerdb.txt\n");
//	}
//	else {
//		printf("load gamecontrollerdb.txt\n");
//	}

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
