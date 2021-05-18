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

#ifndef PHP_STACKDRIVER_DEBUGGER_LOGPOINT_H
#define PHP_STACKDRIVER_DEBUGGER_LOGPOINT_H 1

#include "php.h"
#include "stackdriver_debugger_defines.h"

typedef struct stackdriver_debugger_logpoint_t {
    zend_string *id;
    zend_string *filename;
    zend_long lineno;
    zend_string *condition;
    zend_string *log_level;

    zend_string *format;
    zval callback;

    HashTable *expressions;
} stackdriver_debugger_logpoint_t;

typedef struct stackdriver_debugger_message_t {
    zend_string *filename;
    zend_long lineno;
    zend_string *log_level;

    /* string */
    zval message;

    double timestamp;
} stackdriver_debugger_message_t;

void evaluate_logpoint(zend_execute_data *execute_data, stackdriver_debugger_logpoint_t *logpoint);
int stackdriver_debugger_logpoint_rinit(TSRMLS_D);
int stackdriver_debugger_logpoint_rshutdown(TSRMLS_D);
void list_logpoints(zval *return_value);
int register_logpoint(zend_string *logpoint_id, zend_string *filename,
    zend_long lineno, zend_string *log_level, zend_string *condition,
    zend_string *format, HashTable *expressions, zval *callback);

#endif /* PHP_STACKDRIVER_DEBUGGER_LOGPOINT_H */
