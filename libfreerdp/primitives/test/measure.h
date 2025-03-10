/* measure.h
 * Macros to help with performance measurement.
 * vi:ts=4 sw=4
 *
 * (c) Copyright 2012 Hewlett-Packard Development Company, L.P.
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0.
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * permissions and limitations under the License.  Algorithms used by
 * this code may be covered by patents by HP, Microsoft, or other parties.
 *
 * MEASURE_LOOP_START("measurement", 2000)
 *   code to be measured
 * MEASURE_LOOP_STOP
 *   buffer flush and such
 * MEASURE_SHOW_RESULTS
 *
 * Define GOOGLE_PROFILER if you want gperftools included.
 */

#ifndef TEST_MEASURE_H_INCLUDED
#define TEST_MEASURE_H_INCLUDED

#include <freerdp/config.h>

#include <time.h>
#include <winpr/string.h>
#include <winpr/sysinfo.h>

#ifndef _WIN32
#include <sys/param.h>
#endif

#include <winpr/crt.h>

#ifdef _WIN32

#define PROFILER_START(_prefix_)
#define PROFILER_STOP

#define MEASURE_LOOP_START(_prefix_, _count_)
#define MEASURE_LOOP_STOP
#define MEASURE_GET_RESULTS(_result_)
#define MEASURE_SHOW_RESULTS(_result_)
#define MEASURE_SHOW_RESULTS_SCALED(_scale_, _label_)
#define MEASURE_TIMED(_label_, _init_iter_, _test_time_, _result_, _call_)

#else

#ifdef GOOGLE_PROFILER
#include <gperftools/profiler.h>
#define PROFILER_START(_prefix_)                                  \
	do                                                            \
	{                                                             \
		char _path[PATH_MAX];                                     \
		sprintf_s(_path, sizeof(_path), "./%s.prof", (_prefix_)); \
		ProfilerStart(_path);                                     \
	} while (0);
#define PROFILER_STOP   \
	do                  \
	{                   \
		ProfilerStop(); \
	} while (0);
#else
#define PROFILER_START(_prefix_)
#define PROFILER_STOP
#endif // GOOGLE_PROFILER

extern float measure_delta_time(UINT64 t0, UINT64 t1);
extern void measure_floatprint(float t, char* output, size_t len);

#define MEASURE_LOOP_START(_prefix_, _count_)          \
	{                                                  \
		int _count = (_count_);                        \
		int _loop;                                     \
		char str1[32] = { 0 };                         \
		char str2[32] = { 0 };                         \
		char* _prefix = _strdup(_prefix_);             \
		const UINT64 start = winpr_GetTickCount64NS(); \
		PROFILER_START(_prefix);                       \
		_loop = (_count);                              \
		do                                             \
		{

#define MEASURE_LOOP_STOP \
	}                     \
	while (--_loop)       \
		;

#define MEASURE_GET_RESULTS(_result_)                    \
	PROFILER_STOP;                                       \
	const UINT64 stop = winpr_GetTickCount64NS();        \
	const float delta = measure_delta_time(start, stop); \
	(_result_) = (float)_count / delta;                  \
	free(_prefix);                                       \
	}

#define MEASURE_SHOW_RESULTS(_result_)                                                     \
	PROFILER_STOP;                                                                         \
	const UINT64 stop = winpr_GetTickCount64NS();                                          \
	const float delta = measure_delta_time(start, stop);                                   \
	(_result_) = (float)_count / delta;                                                    \
	measure_floatprint((float)_count / delta, str1);                                       \
	printf("%s: %9d iterations in %5.1f seconds = %s/s \n", _prefix, _count, delta, str1); \
	free(_prefix);                                                                         \
	}

#define MEASURE_SHOW_RESULTS_SCALED(_scale_, _label_)                                            \
	PROFILER_STOP;                                                                               \
	const UINT64 stop = winpr_GetTickCount64NS();                                                \
	const float delta = measure_delta_time(start, stop);                                         \
	measure_floatprint((float)_count / delta, str1);                                             \
	measure_floatprint((float)_count / delta * (_scale_), str2);                                 \
	printf("%s: %9d iterations in %5.1f seconds = %s/s = %s%s \n", _prefix, _count, delta, str1, \
	       str2, _label_);                                                                       \
	free(_prefix);                                                                               \
	}

#define MEASURE_TIMED(_label_, _init_iter_, _test_time_, _result_, _call_) \
	{                                                                      \
		float _r;                                                          \
		MEASURE_LOOP_START(_label_, _init_iter_);                          \
		_call_;                                                            \
		MEASURE_LOOP_STOP;                                                 \
		MEASURE_GET_RESULTS(_r);                                           \
		MEASURE_LOOP_START(_label_, _r* _test_time_);                      \
		_call_;                                                            \
		MEASURE_LOOP_STOP;                                                 \
		MEASURE_SHOW_RESULTS(_result_);                                    \
	}

#endif

#endif // __MEASURE_H_INCLUDED__
