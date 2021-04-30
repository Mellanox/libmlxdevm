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

#ifndef _TS_H
#define _TS_H

#include <time.h>
#include <sys/time.h>
#include <limits.h>

#include "options.h"

struct ts_time {
	long long start, end, latency;
};

struct time_stats {
	struct ts_time total;
	long long min, max, avg;
	long long total_latency;
	long long count;
};

static inline void ts_log_start_time(struct ts_time *s)
{
	s->start = current_time();
}

static inline void ts_log_end_time(struct ts_time *s)
{
	s->end = current_time();
	s->latency = s->end - s->start;
}

static inline void
update_min(const struct ts_time *stat, struct time_stats *t)
{
	if (stat->latency < t->min)
		t->min = stat->latency;
}

static inline void
update_max(const struct ts_time *stat, struct time_stats *t)
{
	if (stat->latency > t->max)
		t->max = stat->latency;
}

static inline void
update_avg(const struct ts_time *stat, struct time_stats *t)
{
	long long avg = t->avg;
	long long old_sum = avg * t->count;
	long long new_avg = (old_sum + stat->latency) / (t->count + 1);

	t->avg = new_avg;
}

static inline void
ts_update_time_stats(const struct ts_time *stat, struct time_stats *t)
{
	update_min(stat, t);
	update_max(stat, t);
	update_avg(stat, t);
	t->total_latency += stat->latency;
	t->count++;
}

static inline void ts_print_lat_stats(const struct time_stats *s, char *str)
{
	printf("%s lat: ", str);
	printf(" min="); print_time(s->min); printf(",");
	printf(" max="); print_time(s->max); printf(",");
	printf(" avg="); print_time(s->avg); printf(",");
	printf(" tot="); print_time(s->total_latency);
	printf("\n");

}

static inline void ts_init(struct time_stats *t)
{
	t->min = LLONG_MAX;
	t->max = LLONG_MIN;
	t->count = 0;
	t->total_latency = 0;
}

#endif
