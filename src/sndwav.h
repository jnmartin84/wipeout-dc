#ifndef SNDWAV_H
#define SNDWAV_H

#include <sys/cdefs.h>
__BEGIN_DECLS

#include <kos/fs.h>

#define WAVE_FORMAT_YAMAHA_ADPCM          0x0020 /* Yamaha ADPCM (ffmpeg) */
typedef struct {
    uint32_t format;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t sample_size;
    uint32_t data_offset;
    uint32_t data_length;
} WavFileInfo;

typedef int wav_stream_hnd_t;

int __attribute__((noinline)) wav_init(void);
void __attribute__((noinline)) wav_shutdown(void);
void __attribute__((noinline)) wav_destroy(void);

wav_stream_hnd_t __attribute__((noinline)) wav_create(const char *filename, int loop, int *duration);

void __attribute__((noinline)) wav_play(void);
void __attribute__((noinline)) wav_pause(void);
void __attribute__((noinline)) wav_stop(void);
void __attribute__((noinline)) wav_volume(int vol);
int __attribute__((noinline)) wav_is_playing(void);

__END_DECLS

#endif
