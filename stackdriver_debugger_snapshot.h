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

#ifndef PHP_STACKDRIVER_DEBUGGER_SNAPSHOT_H
#define PHP_STACKDRIVER_DEBUGGER_SNAPSHOT_H 1

#include "php.h"

typedef struct stackdriver_debugger_variable_t {
    zend_string *name;
    zval value;
    int indirect;
} stackdriver_debugger_variable_t;

typedef struct stackdriver_debugger_stackframe_t {
    zend_string *function;
    zend_string *filename;
    zend_long lineno;
    int locals_count;
    stackdriver_debugger_variable_t **locals;
} stackdriver_debugger_stackframe_t;

/* Snapshot struct */
typedef struct stackdriver_debugger_snapshot_t {
    zend_string *id;
    zend_string *filename;
    zend_long lineno;
    zend_string *condition;
    zend_bool fulfilled;

    zval callback;

    /* index => zval* (strings) */
    HashTable *expressions;

    /* zend_string* (expression) => zval* (result) */
    HashTable *evaluated_expressions;

    int stackframes_count;
    stackdriver_debugger_stackframe_t **stackframes;

} stackdriver_debugger_snapshot_t;

void evaluate_snapshot(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot);
void list_snapshots(zval *return_value);
int register_snapshot(zend_string *snapshot_id, zend_string *filename, zend_long lineno, zend_string *condition, HashTable *expressions, HashTable *callback);

/* request lifecycle callbacks */
int stackdriver_debugger_snapshot_rinit(TSRMLS_D);
int stackdriver_debugger_snapshot_rshutdown(TSRMLS_D);

#endif /* PHP_STACKDRIVER_DEBUGGER_SNAPSHOT_H */
