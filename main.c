#include "audio_reader.h"
#include "cross_correlation.h"
#include "return_codes.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	if (argc < 2 || argc > 3)
	{
		fprintf(stderr, "Invalid arguments\n");
		return ERROR_ARGUMENTS_INVALID;
	}

	AudioArray audio1 = { NULL, 0, 0 };
	AudioArray audio2 = { NULL, 0, 0 };
	int sample_rate;
	int ret1, ret2;
	if (argc == 2)
	{
		sample_rate = find_sample_rate(argv[1]);
		ret1 = read_audio_file(argv[1], &audio1, 0, sample_rate);
		ret2 = read_audio_file(argv[1], &audio2, 1, sample_rate);
	}
	else
	{
		const int sample_rate1 = find_sample_rate(argv[1]);
		const int sample_rate2 = find_sample_rate(argv[2]);
		sample_rate = sample_rate1 > sample_rate2 ? sample_rate1 : sample_rate2;
		ret1 = read_audio_file(argv[1], &audio1, 0, sample_rate);
		ret2 = read_audio_file(argv[2], &audio2, 0, sample_rate);
	}
	if (ret1 != 0 || ret2 != 0)
	{
		free(audio1.data);
		free(audio2.data);
		return ret2;
	}
	const int delta_samples = cross_correlation(audio1.data, audio1.size, audio2.data, audio2.size);
	printf("delta: %i samples\nsample rate: %d Hz\ndelta time: %i ms\n", delta_samples, sample_rate, (int)((double)delta_samples * 1000 / sample_rate));

	free(audio1.data);
	free(audio2.data);
	return SUCCESS;
}
