#include "audio_reader.h"

#include "return_codes.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear_utilities(Utilities *utils)
{
	if (utils->codec_context)
	{
		avcodec_free_context(&utils->codec_context);
	}
	if (utils->format)
	{
		avformat_close_input(&utils->format);
	}
}

static int error(const char *msg, const int error_code, Utilities *utils, SwrContext *swr)
{
	if (swr)
	{
		swr_free(&swr);
	}
	clear_utilities(utils);
	fprintf(stderr, msg);
	return error_code;
}

static void clean_packet(AVPacket *packet, AVFrame *frame)
{
	if (packet)
		av_packet_unref(packet);
	if (frame)
		av_frame_free(&frame);
}

int handle_av_error(const int ret, AVPacket *packet, AVFrame *frame, Utilities *utils, SwrContext *swr)
{
	switch (ret)
	{
	case AVERROR(ENOENT):
		return error("File not found\n", ERROR_CANNOT_OPEN_FILE, utils, swr);

	case AVERROR(ENOMEM):
	{
		clean_packet(packet, frame);
		return error("Not enough memory\n", ERROR_NOTENOUGH_MEMORY, utils, swr);
	}
	case AVERROR_INVALIDDATA:
	{
		clean_packet(packet, frame);
		return error("Invalid data format\n", ERROR_DATA_INVALID, utils, swr);
	}
	case AVERROR(EINVAL):
	{
		clean_packet(packet, frame);
		return error("Invalid arguments\n", ERROR_ARGUMENTS_INVALID, utils, swr);
	}
	case AVERROR(EAGAIN):
	{
		clean_packet(packet, frame);
		return error("Decoder is not ready, try again\n", ERROR_UNSUPPORTED, utils, swr);
	}
	case AVERROR_EOF:
	{
		clean_packet(packet, frame);
		return error("End of file reached, no more frames or packets can be sent\n", ERROR_FORMAT_INVALID, utils, swr);
	}
	default:
	{
		clean_packet(packet, frame);
		return error("Unknown error in %s\n", ERROR_UNKNOWN, utils, swr);
	}
	}
}

static int open_audio_file(const char *path, Utilities *utils)
{
	utils->format = avformat_alloc_context();
	if (!utils->format)
	{
		return error("Cannot allocate audio format context\n", ERROR_FORMAT_INVALID, utils, NULL);
	}
	const int ret = avformat_open_input(&utils->format, path, NULL, NULL);
	if (ret < 0)
		handle_av_error(ret, NULL, NULL, utils, NULL);
	if (avformat_find_stream_info(utils->format, NULL) < 0)
	{
		return error("Cannot find stream information\n", ERROR_FORMAT_INVALID, utils, NULL);
	}
	utils->stream_index = av_find_best_stream(utils->format, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (utils->stream_index < 0)
	{
		return error("Cannot find stream in '%s'\n", ERROR_FORMAT_INVALID, utils, NULL);
	}
	const AVStream *stream = utils->format->streams[utils->stream_index];
	utils->codec_context = avcodec_alloc_context3(NULL);
	if (!utils->codec_context)
	{
		return error("Cannot allocate audio codec context\n", ERROR_FORMAT_INVALID, utils, NULL);
	}
	if (avcodec_parameters_to_context(utils->codec_context, stream->codecpar) < 0)
	{
		return error("Cannot copy codec parameters to context\n", ERROR_FORMAT_INVALID, utils, NULL);
	}
	if (avcodec_open2(utils->codec_context, avcodec_find_decoder(utils->codec_context->codec_id), NULL) < 0)
	{
		return error("Cannot open decoder for stream", ERROR_FORMAT_INVALID, utils, NULL);
	}
	return 0;
}

static SwrContext *init_swr_context(Utilities *utils, const int out_sample_rate, int channel_map[])
{
	SwrContext *swr = swr_alloc();
	if (!swr)
	{
		return NULL;
	}
	if (av_opt_set_int(swr, "in_channel_count", utils->codec_context->channels, 0) < 0 ||
		av_opt_set_int(swr, "out_channel_count", 1, 0) < 0 ||
		av_opt_set_int(swr, "in_sample_rate", utils->codec_context->sample_rate, 0) < 0 ||
		av_opt_set_int(swr, "out_sample_rate", out_sample_rate, 0) < 0 ||
		av_opt_set_sample_fmt(swr, "in_sample_fmt", utils->codec_context->sample_fmt, 0) < 0 ||
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_DBL, 0) < 0 || swr_set_channel_mapping(swr, channel_map) < 0)
	{
		error("Cannot set channel mapping\n", ERROR_FORMAT_INVALID, utils, swr);
		return NULL;
	}

	if (swr_init(swr) < 0)
	{
		swr_free(&swr);
		return NULL;
	}
	return swr;
}

static int process_audio_frames(Utilities *utils, SwrContext *swr, AudioArray *audio_array, const int stream_index)
{
	AVPacket packet;
	AVFrame *frame = av_frame_alloc();
	if (!frame)
	{
		return error("Cannot allocate audio frame\n", ERROR_FORMAT_INVALID, utils, swr);
	}
	double *buffer;
	size_t data_capacity = 1024;
	audio_array->data = (double *)malloc(data_capacity * sizeof(double));
	audio_array->size = 0;
	utils->codec_context->pkt_timebase = utils->format->streams[0]->time_base;

	while (av_read_frame(utils->format, &packet) >= 0)
	{
		if (packet.stream_index != stream_index)
		{
			av_packet_unref(&packet);
			continue;
		}

		int ret = avcodec_send_packet(utils->codec_context, &packet);
		if (ret < 0)
		{
			return handle_av_error(ret, &packet, frame, utils, swr);
		}

		while (avcodec_receive_frame(utils->codec_context, frame) == 0)
		{
			ret = av_samples_alloc((uint8_t **)&buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_DBL, 0);
			if (ret < 0)
			{
				return handle_av_error(ret, &packet, frame, utils, swr);
			}
			const int frame_count = swr_convert(swr, (uint8_t **)&buffer, frame->nb_samples, frame->data, frame->nb_samples);

			if (frame_count > 0)
			{
				if (audio_array->size + frame_count >= data_capacity)
				{
					audio_array->data = (double *)realloc(audio_array->data, data_capacity * 2 * sizeof(double));
					data_capacity *= 2;
				}
				if (!audio_array->data)
				{
					av_packet_unref(&packet);
					av_freep(&buffer);
					av_frame_free(&frame);
					return error("Cannot reallocate data\n", ERROR_FORMAT_INVALID, utils, swr);
				}
				memcpy(audio_array->data + audio_array->size, buffer, frame_count * sizeof(double));
				audio_array->size += frame_count;
			}
		}
		av_freep(&buffer);
		av_packet_unref(&packet);
	}
	av_frame_free(&frame);
	return SUCCESS;
}

int read_audio_file(const char *path, AudioArray *audio_array, const int flag, const int new_sample_rate)
{
	Utilities utils = { NULL, NULL, 0, 0, 0 };
	int ret = open_audio_file(path, &utils);
	if (ret > 0)
	{
		return ret;
	}
	int channel_map[] = { flag, -1 };
	const int total_channels = utils.codec_context->channels;
	if (flag == 1 && total_channels < 2)
	{
		ret = error("Invalid channel", ERROR_FORMAT_INVALID, &utils, NULL);
		goto cleanup;
	}
	SwrContext *swr = init_swr_context(&utils, new_sample_rate, channel_map);
	if (!swr)
	{
		ret = error("Cannot init SwrContest \n", ERROR_FORMAT_INVALID, &utils, swr);
		goto cleanup;
	}
	audio_array->sample_rate = new_sample_rate;
	ret = process_audio_frames(&utils, swr, audio_array, flag);
	if (ret > 0)
	{
		ret = error("Cannot process audio \n", ret, &utils, swr);
		goto cleanup;
	}

	swr_free(&swr);
	clear_utilities(&utils);
	return 0;

cleanup:
	free(audio_array->data);
	swr_free(&swr);
	clear_utilities(&utils);
	return ret;
}

int find_sample_rate(const char *path)
{
	Utilities utils = { NULL, NULL, 0, 0, 0 };
	const int ret = open_audio_file(path, &utils);
	if (ret > 0)
	{
		return ret;
	}
	const int sample_rate = utils.codec_context->sample_rate;
	clear_utilities(&utils);
	return sample_rate;
}
