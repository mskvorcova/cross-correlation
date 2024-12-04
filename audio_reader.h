#ifndef AUDIO_READER_H
#define AUDIO_READER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct
{
	double *data;
	size_t size;
	int sample_rate;
} AudioArray;

typedef struct
{
	AVFormatContext *format;
	AVCodecContext *codec_context;
	int stream_index;
	int sample_rate;
	int channels;
} Utilities;

int read_audio_file(const char *path, AudioArray *audio_array, int flag, int new_sample_rate);
int find_sample_rate(const char *path);
#endif	  // AUDIO_READER_H
