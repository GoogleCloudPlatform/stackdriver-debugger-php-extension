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

#ifndef PHP_STACKDRIVER_DEFINES_H
#define PHP_STACKDRIVER_DEFINES_H 1

#if PHP_VERSION_ID < 80000
#define STACKDRIVER_OBJ_P(v) (v)
#ifndef Z_LINENO
#define Z_LINENO(zval) (zval).u2.lineno
#endif
#else
#define STACKDRIVER_OBJ_P(v) Z_OBJ_P(v)

#ifndef TSRMLS_D
#define TSRMLS_D void
#define TSRMLS_DC
#define TSRMLS_C
#define TSRMLS_CC
#define TSRMLS_FETCH()
#endif
#endif

#endif
