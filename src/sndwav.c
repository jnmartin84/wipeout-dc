#include <kos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <kos/thread.h>
#include <dc/sound/stream.h>


#include "sndwav.h"
#include "wipeout/game.h"

/* Keep track of things from the Driver side */
#define SNDDRV_STATUS_NULL 0x00
#define SNDDRV_STATUS_READY 0x01
#define SNDDRV_STATUS_DONE 0x02

/* Keep track of things from the Decoder side */
#define SNDDEC_STATUS_NULL 0x00
#define SNDDEC_STATUS_READY 0x01
#define SNDDEC_STATUS_STREAMING 0x02
#define SNDDEC_STATUS_PAUSING 0x03
#define SNDDEC_STATUS_STOPPING 0x04
#define SNDDEC_STATUS_RESUMING 0x05

typedef void *(*snddrv_cb)(snd_stream_hnd_t, int, int *);

typedef struct
{
	/* The buffer on the AICA side */
	snd_stream_hnd_t shnd;

	/* We either read the wav data from a file or
	   we read from a buffer */
	file_t wave_file;

	/* Contains the buffer that we are going to send
	   to the AICA in the callback.  Should be 32-byte
	   aligned */
	uint8_t *drv_buf;

	/* Status of the stream that can be started, stopped
	   paused, ready. etc */
	volatile int status;

	snddrv_cb callback;

	uint32_t loop;
	uint32_t vol; /* 0-255 */

	uint32_t format;	  /* Wave format */
	uint32_t channels;	  /* 1-Mono/2-Stereo */
	uint32_t sample_rate; /* 44100Hz */
	uint32_t sample_size; /* 4/8/16-Bit */

	/* Offset into the file or buffer where the audio
	   data starts */
	uint32_t data_offset;

	/* The length of the audio data */
	uint32_t data_length;

	/* Used only in reading wav data from a buffer
	   and not a file */
	uint32_t buf_offset;
} snddrv_hnd;

static snddrv_hnd stream;
static volatile int sndwav_status = SNDDRV_STATUS_NULL;
static kthread_t *audio_thread;
static kthread_attr_t audio_attr;
static mutex_t stream_mutex;

static void *sndwav_thread(void *param);
static void *wav_file_callback(snd_stream_hnd_t hnd, int req, int *done);

int __attribute__((noinline)) wav_init(void)
{
#if RANGECHECK
	mutex_init(&stream_mutex, MUTEX_TYPE_ERRORCHECK);
#else
	mutex_init(&stream_mutex, MUTEX_TYPE_NORMAL);
#endif

	stream.shnd = SND_STREAM_INVALID;
	stream.vol = 0;
	stream.status = SNDDEC_STATUS_NULL;
	stream.callback = NULL;
	audio_attr.create_detached = 0;
	audio_attr.stack_size = 32768;
	audio_attr.stack_ptr = NULL;
	audio_attr.prio = PRIO_DEFAULT;
	audio_attr.label = "MusicPlayer";

	audio_thread = thd_create_ex(&audio_attr, sndwav_thread, NULL);
	if (audio_thread != NULL)
		sndwav_status = SNDDRV_STATUS_READY;

	return sndwav_status;
}

void __attribute__((noinline)) wav_shutdown(void)
{
	sndwav_status = SNDDRV_STATUS_DONE;

	thd_join(audio_thread, NULL);

	wav_destroy();

	if (stream.drv_buf) {
		free(stream.drv_buf);
		stream.drv_buf = NULL;
	}
}

void __attribute__((noinline)) wav_destroy(void)
{
	if (stream.shnd == SND_STREAM_INVALID)
		return;

#if RANGECHECK
	if (mutex_lock(&stream_mutex))
		die("Failed to lock stream_mutex");
#else
	mutex_lock(&stream_mutex);
#endif

	snd_stream_destroy(stream.shnd);
	stream.shnd = SND_STREAM_INVALID;
	stream.status = SNDDEC_STATUS_NULL;
	stream.vol = 0;
	stream.callback = NULL;

	if (stream.wave_file != FILEHND_INVALID)
		fs_close(stream.wave_file);


#if RANGECHECK
	if (mutex_unlock(&stream_mutex))
		die("Failed to unlock stream_mutex");
#else
	mutex_unlock(&stream_mutex);
#endif
}

static int wav_get_info_adpcm(file_t file, WavFileInfo *result) {
    result->format = WAVE_FORMAT_YAMAHA_ADPCM;
    result->channels = 2;
    result->sample_rate = 44100;
    result->sample_size = 4;
    result->data_length = fs_total(file);

    result->data_offset = 0;

    return 1;
}
extern save_t save;


wav_stream_hnd_t __attribute__((noinline)) wav_create(const char *filename, int loop, int *duration)
{
	file_t file;
	WavFileInfo info;
	wav_stream_hnd_t index;

	if (filename == NULL)
		return SND_STREAM_INVALID;

	file = fs_open(filename, O_RDONLY);

	if (file == FILEHND_INVALID)
		return SND_STREAM_INVALID;

	index = snd_stream_alloc(wav_file_callback, SND_STREAM_BUFFER_MAX);
	if (index == SND_STREAM_INVALID) {
		fs_close(file);
		snd_stream_destroy(index);
		return SND_STREAM_INVALID;
	}

	wav_get_info_adpcm(file, &info);

	float samps = (float)info.data_length;
	float runtime = ceilf(samps/44100.0f);
	*duration = (int)runtime;

	if (!stream.drv_buf) {
		stream.drv_buf = memalign(32, SND_STREAM_BUFFER_MAX);
	}

	if (stream.drv_buf == NULL) {
		fs_close(file);
		snd_stream_destroy(index);
		return SND_STREAM_INVALID;
	}

	stream.shnd = index;
	stream.wave_file = file;
	stream.loop = loop;
	stream.callback = wav_file_callback;
	stream.vol = 192 * save.music_volume;
	stream.format = info.format;
	stream.channels = info.channels;
	stream.sample_rate = info.sample_rate;
	stream.sample_size = info.sample_size;
	stream.data_length = info.data_length;
	stream.data_offset = info.data_offset;

	fs_seek(stream.wave_file, stream.data_offset, SEEK_SET);

	snd_stream_volume(stream.shnd, stream.vol);

	stream.status = SNDDEC_STATUS_READY;

	return index;
}

void __attribute__((noinline)) wav_play(void)
{
	if (stream.status == SNDDEC_STATUS_STREAMING)
		return;

	stream.status = SNDDEC_STATUS_RESUMING;
}

void __attribute__((noinline)) wav_play_volume(void)
{
	if (stream.status == SNDDEC_STATUS_STREAMING)
		return;

	stream.status = SNDDEC_STATUS_RESUMING;
}

void __attribute__((noinline)) wav_pause(void)
{
	if (stream.status == SNDDEC_STATUS_READY || stream.status == SNDDEC_STATUS_PAUSING)
		return;

	stream.status = SNDDEC_STATUS_PAUSING;
}

void __attribute__((noinline)) wav_stop(void)
{
	if (stream.status == SNDDEC_STATUS_READY || stream.status == SNDDEC_STATUS_STOPPING)
		return;

	stream.status = SNDDEC_STATUS_STOPPING;
}

void __attribute__((noinline)) wav_volume(int vol)
{
	if (stream.shnd == SND_STREAM_INVALID)
		return;

	if (vol > 255)
		vol = 255;

	if (vol < 0)
		vol = 0;

	stream.vol = vol;
	snd_stream_volume(stream.shnd, stream.vol);
}

int __attribute__((noinline)) wav_is_playing(void)
{
	return stream.status == SNDDEC_STATUS_STREAMING;
}

static void *sndwav_thread(void *param)
{
	(void)param;

	while (sndwav_status != SNDDRV_STATUS_DONE) {
#if RANGECHECK
		if (mutex_lock(&stream_mutex))
			die("Failed to lock stream_mutex");
#else
		mutex_lock(&stream_mutex);
#endif
		switch (stream.status) {
		case SNDDEC_STATUS_RESUMING:
			snd_stream_volume(stream.shnd, stream.vol);
			snd_stream_start_adpcm(stream.shnd, stream.sample_rate, stream.channels - 1);
			snd_stream_volume(stream.shnd, stream.vol);			
			stream.status = SNDDEC_STATUS_STREAMING;
			break;
		case SNDDEC_STATUS_PAUSING:
			snd_stream_stop(stream.shnd);
			stream.status = SNDDEC_STATUS_READY;
			break;
		case SNDDEC_STATUS_STOPPING:
			snd_stream_stop(stream.shnd);
			if (stream.wave_file != FILEHND_INVALID)
				fs_seek(stream.wave_file, stream.data_offset, SEEK_SET);
			else
				stream.buf_offset = stream.data_offset;

			stream.status = SNDDEC_STATUS_READY;
			break;
		case SNDDEC_STATUS_STREAMING:
			snd_stream_poll(stream.shnd);
			break;
		case SNDDEC_STATUS_READY:
		default:
			break;
		}

#if RANGECHECK
		if (mutex_unlock(&stream_mutex))
			die("Failed to unlock stream_mutex");
#else
		mutex_unlock(&stream_mutex);
#endif
		thd_sleep(25);
	}

	return NULL;
}

static void *wav_file_callback(snd_stream_hnd_t hnd, int req, int *done)
{
	(void)hnd;
	ssize_t read = fs_read(stream.wave_file, stream.drv_buf, req);

	if (read == -1) {
#if RANGECHECK
		die("Failed to read from stream wave_file");
#endif
		snd_stream_stop(stream.shnd);
		stream.status = SNDDEC_STATUS_READY;
		return NULL;
	}

	if (read != req) {
		fs_seek(stream.wave_file, stream.data_offset, SEEK_SET);

		if (stream.loop) {
			ssize_t read2 = fs_read(stream.wave_file, stream.drv_buf, req);

			if (read2 == -1) {
#if RANGECHECK
				die("read != req: Failed to read from stream wave_file");
#endif
				snd_stream_stop(stream.shnd);
				stream.status = SNDDEC_STATUS_READY;
				return NULL;
			}
		} else {
			snd_stream_stop(stream.shnd);
			stream.status = SNDDEC_STATUS_READY;
			return NULL;
		}
	}

	*done = req;

	return stream.drv_buf;
}
