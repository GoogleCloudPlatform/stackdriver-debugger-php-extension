/*
 * Copyright 2017 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PHP_STACKDRIVER_DEBUGGER_TIME_FUNCTIONS_H
#define PHP_STACKDRIVER_DEBUGGER_TIME_FUNCTIONS_H 1

#ifdef _WIN32
#include "win32/time.h"
#else
#include <sys/time.h>
#endif

/* Return the current timestamp as a double */
static double stackdriver_debugger_now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (double) (tv.tv_sec + tv.tv_usec / 1000000.00);
}

#endif /* PHP_STACKDRIVER_DEBUGGER_TIME_FUNCTIONS_H */
