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

#ifndef PHP_STACKDRIVER_DEBUGGER_H
#define PHP_STACKDRIVER_DEBUGGER_H 1

#include "stackdriver_debugger_snapshot.h"

/* Debugger functions */
PHP_FUNCTION(stackdriver_debugger_snapshot);
PHP_FUNCTION(stackdriver_debugger_add_snapshot);
PHP_FUNCTION(stackdriver_debugger_list_snapshots);
PHP_FUNCTION(stackdriver_debugger_logpoint);
PHP_FUNCTION(stackdriver_debugger_add_logpoint);
PHP_FUNCTION(stackdriver_debugger_list_logpoints);
PHP_FUNCTION(stackdriver_debugger_valid_statement);
PHP_FUNCTION(stackdriver_debugger_usage);

#endif
