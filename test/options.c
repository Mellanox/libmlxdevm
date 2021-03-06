/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its
 * affiliates (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include <stdio.h>
#include <sys/time.h>

#define NSEC_PER_SEC	1000000000ll

long long current_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_usec * 1000ll + tv.tv_sec * NSEC_PER_SEC;
}

struct suffix_map {
	long long multiplier;
	const char *suffix;
};

const static struct suffix_map time_suffix[] = {
	{ NSEC_PER_SEC * 60, "min" },
	{ NSEC_PER_SEC , "sec" },
	{ 1000000ll, "msec" },
	{ 1000ll, "usec" },
	{ 1ll, "nsec" },
	{ 0, "sec" },
};

void print_time(long long val)
{
	const struct suffix_map *sfx = time_suffix;
	int precision;

	while (val < sfx->multiplier && sfx->multiplier > 1)
		sfx++;

	if (val % sfx->multiplier == 0)
		precision = 0;
	else if (val >= sfx->multiplier * 10)
		precision = 1;
	else
		precision = 2;

	printf("%.*f%s ", precision, val * 1.0 / sfx->multiplier, sfx->suffix);
}
