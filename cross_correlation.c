#include "cross_correlation.h"

#include "return_codes.h"

#include <fftw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void initialize_input_data(const double *signal, const size_t size, double *input_data, const int n)
{
	memcpy(input_data, signal, size * sizeof(double));
	if (n > size)
	{
		memset(input_data + size, 0, (n - size) * sizeof(double));
	}
}

static int perform_fft(double *input_data, fftw_complex *result, const size_t n)
{
	const fftw_plan plan = fftw_plan_dft_r2c_1d(n, input_data, result, FFTW_ESTIMATE);
	if (!plan)
	{
		return ERROR_UNSUPPORTED;
	}
	fftw_execute(plan);
	fftw_destroy_plan(plan);
	fftw_cleanup();
	return SUCCESS;
}

static void calculate_correlation(const fftw_complex *result1, const fftw_complex *result2, fftw_complex *corr, const size_t n)
{
	for (size_t i = 0; i < (n / 2 + 1); i++)
	{
		const double real_part = result1[i][0] * result2[i][0] + result1[i][1] * result2[i][1];
		const double imag_part = (-result1[i][0] * result2[i][1]) + result1[i][1] * result2[i][0];
		corr[i][0] = real_part;
		corr[i][1] = imag_part;
	}
}

static double *compute_inverse_fft(fftw_complex *corr, const size_t n)
{
	double *final_data = malloc(sizeof(double) * n);
	if (!final_data)
	{
		fprintf(stderr, "Cannot allocate memory in inverse FFT.\n");
		return NULL;
	}
	const fftw_plan plan_corr = fftw_plan_dft_c2r_1d(n, corr, final_data, FFTW_ESTIMATE);
	if (!plan_corr)
	{
		free(final_data);
		fprintf(stderr, "Cannot create plan for inverse FFT.\n");
		return NULL;
	}
	fftw_execute(plan_corr);
	fftw_destroy_plan(plan_corr);
	return final_data;
}

static size_t find_max_index(const double *data, const size_t size, double *max_value)
{
	size_t max_index = 0;
	*max_value = data[0];

	for (size_t i = 1; i < size; i++)
	{
		if (data[i] > *max_value)
		{
			*max_value = data[i];
			max_index = i;
		}
	}
	return max_index;
}

int cross_correlation(const double *signal1, const size_t size1, const double *signal2, const size_t size2)
{
	const int n = size1 + size2 - 1;
	double *input_data = malloc(2 * n * sizeof(double));
	double *input_data1 = input_data;
	double *input_data2 = input_data + n;
	fftw_complex *results = fftw_malloc(3 * n * sizeof(fftw_complex));
	fftw_complex *result1 = results;
	fftw_complex *result2 = results + n;
	fftw_complex *corr = results + 2 * n;

	if (!input_data1 || !input_data2 || !result1 || !result2 || !corr)
	{
		fprintf(stderr, "Cannot allocate memory for cross correlation.\n");
		goto cleanup;
	}

	initialize_input_data(signal1, size1, input_data1, n);
	initialize_input_data(signal2, size2, input_data2, n);
	if (perform_fft(input_data1, result1, n) != SUCCESS)
	{
		fprintf(stderr, "Cannot execute FFT for first signal.\n");
		goto cleanup;
	}
	if (perform_fft(input_data2, result2, n) != SUCCESS)
	{
		fprintf(stderr, "Cannot execute FFT for second signal.\n");
		goto cleanup;
	}

	calculate_correlation(result1, result2, corr, n);

	double *final_data = compute_inverse_fft(corr, n);
	if (!final_data)
	{
		fprintf(stderr, "Cannot perform inverse FFT.\n");
		goto cleanup;
	}
	double max_corr = 0.0;
	const size_t max_index = find_max_index(final_data, n, &max_corr);
	free(final_data);
	free(input_data);
	fftw_free(results);
	return max_index > size1 ? max_index - n : max_index;
cleanup:
	free(input_data);
	fftw_free(results);
	free(final_data);
	return 0;
}
