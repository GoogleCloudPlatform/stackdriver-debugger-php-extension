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

#ifndef PHP_STACKDRIVER_H
#define PHP_STACKDRIVER_H 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "stackdriver_debugger.h"

#define PHP_STACKDRIVER_DEBUGGER_VERSION "0.2.0"
#define PHP_STACKDRIVER_DEBUGGER_EXTNAME "stackdriver_debugger"
#define PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_FUNCTIONS "stackdriver_debugger.function_whitelist"
#define PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_METHODS "stackdriver_debugger.method_whitelist"
#define PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME "stackdriver_debugger.max_time"
#define PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME_PERCENTAGE "stackdriver_debugger.max_time_percentage"
#define PHP_STACKDRIVER_DEBUGGER_INI_MAX_MEMORY "stackdriver_debugger.max_memory"

PHP_FUNCTION(stackdriver_debugger_version);

extern zend_module_entry stackdriver_debugger_module_entry;
#define phpext_stackdriver_debugger_ptr &stackdriver_debugger_module_entry

PHP_MINIT_FUNCTION(stackdriver_debugger);
PHP_MSHUTDOWN_FUNCTION(stackdriver_debugger);
PHP_RINIT_FUNCTION(stackdriver_debugger);
PHP_RSHUTDOWN_FUNCTION(stackdriver_debugger);

ZEND_BEGIN_MODULE_GLOBALS(stackdriver_debugger)
    /* map of function name -> empty null zval */
    HashTable *user_whitelisted_functions;

    /* map of method name -> empty null zval */
    HashTable *user_whitelisted_methods;

    /* map of filename -> stackdriver_debugger_snapshot[] */
    HashTable *snapshots_by_file;

    /* map of snapshot id -> stackdriver_debugger_snapshot */
    HashTable *snapshots_by_id;

    /* map of snapshot id -> stackdriver_debugger_snapshot */
    HashTable *collected_snapshots_by_id;

    /* map of filename -> stackdriver_debugger_logpoint[] */
    HashTable *logpoints_by_file;

    /* map of snapshot id -> stackdriver_debugger_logpoint */
    HashTable *logpoints_by_id;

    /* array of stackdriver_debugger_message_t */
    HashTable *collected_messages;

    /* array of pointers to ast node types */
    HashTable *ast_to_clean;

    double time_spent;
    double request_start;
    size_t memory_used;
    size_t max_memory;
    zend_bool opcache_enabled;
ZEND_END_MODULE_GLOBALS(stackdriver_debugger)

extern ZEND_DECLARE_MODULE_GLOBALS(stackdriver_debugger)

#ifdef ZTS
#define        STACKDRIVER_DEBUGGER_G(v)        TSRMG(stackdriver_debugger_globals_id, zend_stackdriver_debugger_globals *, v)
#else
#define        STACKDRIVER_DEBUGGER_G(v)        (stackdriver_debugger_globals.v)
#endif

#endif /* PHP_STACKDRIVER_H */
