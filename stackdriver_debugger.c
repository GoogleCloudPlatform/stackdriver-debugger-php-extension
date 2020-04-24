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

#include "php.h"
#include "main/SAPI.h"
#include "standard/php_string.h"
#include "standard/info.h"
#include "php_stackdriver_debugger.h"
#include "stackdriver_debugger.h"
#include "stackdriver_debugger_ast.h"
#include "stackdriver_debugger_logpoint.h"
#include "stackdriver_debugger_snapshot.h"
#include "zend_exceptions.h"
#include "stackdriver_debugger_time_functions.h"
#include "zend_alloc.h"

ZEND_DECLARE_MODULE_GLOBALS(stackdriver_debugger)

ZEND_BEGIN_ARG_INFO_EX(arginfo_stackdriver_debugger_snapshot, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, snapshotId, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stackdriver_debugger_add_snapshot, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, filename, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, line, IS_LONG, 0)
    ZEND_ARG_ARRAY_INFO(0, options, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stackdriver_debugger_logpoint, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, logpointId, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stackdriver_debugger_add_logpoint, 0, 0, 4)
    ZEND_ARG_TYPE_INFO(0, filename, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, line, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, logLevel, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, format, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, options, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_stackdriver_debugger_valid_statement, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, statement, IS_STRING, 0)
ZEND_END_ARG_INFO()


/* List of functions provided by this extension */
static zend_function_entry stackdriver_debugger_functions[] = {
    PHP_FE(stackdriver_debugger_version, NULL)
    PHP_FE(stackdriver_debugger_snapshot, arginfo_stackdriver_debugger_snapshot)
    PHP_FE(stackdriver_debugger_add_snapshot, arginfo_stackdriver_debugger_add_snapshot)
    PHP_FE(stackdriver_debugger_list_snapshots, NULL)
    PHP_FE(stackdriver_debugger_logpoint, arginfo_stackdriver_debugger_logpoint)
    PHP_FE(stackdriver_debugger_add_logpoint, arginfo_stackdriver_debugger_add_logpoint)
    PHP_FE(stackdriver_debugger_list_logpoints, NULL)
    PHP_FE(stackdriver_debugger_valid_statement, arginfo_stackdriver_debugger_valid_statement)
    PHP_FE_END
};

PHP_MINFO_FUNCTION(stackdriver_debugger)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Stackdriver Debugger support", "enabled");
    php_info_print_table_row(2, "Stackdriver Debugger module version", PHP_STACKDRIVER_DEBUGGER_VERSION);
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}

/* Registers the lifecycle hooks for this extension */
zend_module_entry stackdriver_debugger_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_STACKDRIVER_DEBUGGER_EXTNAME,
    stackdriver_debugger_functions,
    PHP_MINIT(stackdriver_debugger),
    PHP_MSHUTDOWN(stackdriver_debugger),
    PHP_RINIT(stackdriver_debugger),
    PHP_RSHUTDOWN(stackdriver_debugger),
    PHP_MINFO(stackdriver_debugger),
    PHP_STACKDRIVER_DEBUGGER_VERSION,
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(stackdriver_debugger)

PHP_INI_MH(OnUpdate_stackdriver_debugger_max_time);
PHP_INI_MH(OnUpdate_stackdriver_debugger_max_time_percentage);
PHP_INI_MH(OnUpdate_stackdriver_debugger_max_memory);

/* Registers php.ini directives */
PHP_INI_BEGIN()
    PHP_INI_ENTRY(PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_FUNCTIONS, NULL, PHP_INI_ALL, OnUpdate_stackdriver_debugger_whitelisted_functions)
    PHP_INI_ENTRY(PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_METHODS, NULL, PHP_INI_ALL, OnUpdate_stackdriver_debugger_whitelisted_methods)
    PHP_INI_ENTRY(PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME, "10", PHP_INI_ALL, NULL)
    PHP_INI_ENTRY(PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME_PERCENTAGE, "1", PHP_INI_ALL, NULL)
    PHP_INI_ENTRY(PHP_STACKDRIVER_DEBUGGER_INI_MAX_MEMORY, "10", PHP_INI_ALL, OnUpdate_stackdriver_debugger_max_memory)
PHP_INI_END()

/**
 * Return the current version of the stackdriver_debugger extension
 *
 * @return string
 */
PHP_FUNCTION(stackdriver_debugger_version)
{
    RETURN_STRING(PHP_STACKDRIVER_DEBUGGER_VERSION);
}

/* Constructor used for creating the stackdriver_debugger globals */
static void php_stackdriver_debugger_globals_ctor(void *pDest TSRMLS_DC)
{
    zend_stackdriver_debugger_globals *stackdriver_debugger_global = (zend_stackdriver_debugger_globals *) pDest;
}

static double stackdriver_debugger_total_time_spent;
static int stackdriver_debugger_total_requests_handled;

/**
 * Returns the max time that should be spent in the debugger in milliseconds.
 *
 * @return double
 */
static double stackdriver_debugger_max_time()
{
    double percentage, max_time = INI_FLT(PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME) * 0.001;
    if (stackdriver_debugger_total_requests_handled > 0) {
        percentage = 0.01 * INI_FLT(PHP_STACKDRIVER_DEBUGGER_INI_MAX_TIME_PERCENTAGE) * stackdriver_debugger_total_time_spent / stackdriver_debugger_total_requests_handled;
        if (percentage < max_time) {
            return percentage;
        }
    }
    return max_time;
}

/**
 * Detects if opcache is available and enabled.
 *
 * @return zend_bool
 */
static zend_bool stackdriver_debugger_opcache_enabled()
{
    zend_string *function_name = zend_string_init("opcache_invalidate", sizeof("opcache_invalidate") - 1, 0);
    zend_function *invalidate = zend_hash_find_ptr(EG(function_table), function_name);
    zend_bool enabled;
    zend_string_release(function_name);
    if (strcmp(sapi_module.name, "cli") == 0) {
        enabled = INI_BOOL("opcache.enable_cli");
    } else {
        enabled = INI_BOOL("opcache.enable");
    }
    return invalidate != NULL && enabled;
}

/**
 * Helper function to invoke `opcache_invalidate` from our C functions. Returns
 * SUCCESS | FAILURE.
 */
static int stackdriver_debugger_opcache_invalidate(zend_string *filename)
{
    zval params[2], function_name, ret;
    if (STACKDRIVER_DEBUGGER_G(opcache_enabled)) {
        ZVAL_STRING(&function_name, "opcache_invalidate");
        ZVAL_STR(&params[0], filename);
        ZVAL_BOOL(&params[1], 1);
        return call_user_function(EG(function_table), NULL, &function_name, &ret, 2, params);
    }
    return FAILURE;
}

/**
 * Return the collected list of debugger snapshots that have been collected for
 * this request.
 *
 * @return array
 */
PHP_FUNCTION(stackdriver_debugger_list_snapshots)
{
    array_init(return_value);
    list_snapshots(return_value);
}

/**
 * Return the collected list of logpoint messages that have been collected for
 * this request.
 *
 * @return array
 */
PHP_FUNCTION(stackdriver_debugger_list_logpoints)
{
    array_init(return_value);
    list_logpoints(return_value);
}

/**
 * Returns whether or not the provided PHP statement can be used for a
 * breakpoint condition or as an evaluated expression.
 *
 * @param string $statement
 * @return boolean
 */
PHP_FUNCTION(stackdriver_debugger_valid_statement)
{
    zend_string *statement = NULL;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &statement) == FAILURE) {
        RETURN_FALSE;
    }

    if (valid_debugger_statement(statement) != SUCCESS) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}

/**
 * Executes the provided statement in the current execution context. Returns
 * SUCCESS | FAILURE.
 */
static int test_conditional(zend_string *statement)
{
    if (statement == NULL) {
        return SUCCESS;
    }

    zval retval;

    if (zend_eval_string(ZSTR_VAL(statement), &retval, "conditional") == SUCCESS) {
        /*
         * If there is an exception thrown in the conditional, we will ignore
         * it. An exception is unexpected as we validate the type of AST
         * commands allowed and "throw" is not allowed.
         *
         * Caveat: you may see warning messages for things like accessing
         * properties on non-objects, but they should not show in the output
         * unless the user's PHP configuration displays warning, info, notice,
         * or debug messages. This scenario is unlikely for production
         * environments.
         */
        if (EG(exception) != NULL) {
            zend_clear_exception();
            return FAILURE;
        }

        /* internal function used to cast zval to boolean */
        convert_to_boolean(&retval);

        if (Z_TYPE(retval) == IS_TRUE) {
            return SUCCESS;
        } else {
            return FAILURE;
        }
    }

    php_error_docref(NULL, E_WARNING, "Failed to execute conditional.");
    return FAILURE;
}

/**
 * Capture the execution state for the provided snapshotId.
 *
 * @param string $snapshotId
 * @return boolean
 */
PHP_FUNCTION(stackdriver_debugger_snapshot)
{
    zend_string *snapshot_id = NULL;
    stackdriver_debugger_snapshot_t *snapshot;
    double start = 0;
    size_t start_memory = 0, end_memory;

    // if we've already spent more than the time allowed, skip further breakpoints
    if (STACKDRIVER_DEBUGGER_G(time_spent) > stackdriver_debugger_max_time()) {
        RETURN_FALSE;
    }

    // if we've already spent more than the time allowed, skip further breakpoints
    if (STACKDRIVER_DEBUGGER_G(memory_used) > STACKDRIVER_DEBUGGER_G(max_memory)) {
        RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &snapshot_id) == FAILURE) {
        RETURN_FALSE;
    }

    start = stackdriver_debugger_now();
    start_memory = zend_memory_usage(0);
    snapshot = zend_hash_find_ptr(STACKDRIVER_DEBUGGER_G(snapshots_by_id), snapshot_id);

    if (snapshot == NULL || snapshot->fulfilled || test_conditional(snapshot->condition) != SUCCESS) {
        STACKDRIVER_DEBUGGER_G(time_spent) = STACKDRIVER_DEBUGGER_G(time_spent) + stackdriver_debugger_now() - start;
        RETURN_FALSE;
    }

    evaluate_snapshot(execute_data, snapshot);
    STACKDRIVER_DEBUGGER_G(time_spent) = STACKDRIVER_DEBUGGER_G(time_spent) + stackdriver_debugger_now() - start;
    end_memory = zend_memory_usage(0);
    if (end_memory > start_memory) {
        STACKDRIVER_DEBUGGER_G(memory_used) = STACKDRIVER_DEBUGGER_G(memory_used) + end_memory - start_memory;
    }

    RETURN_TRUE;
}

/**
 * Evaluate the logpoint for the provided logpointId.
 *
 * @param string $logpointId
 * @return boolean
 */
PHP_FUNCTION(stackdriver_debugger_logpoint)
{
    zend_string *logpoint_id = NULL;
    stackdriver_debugger_logpoint_t *logpoint;
    double start = 0;
    size_t start_memory = 0, end_memory;

    // if we've already spent more than the time allowed, skip further breakpoints
    if (STACKDRIVER_DEBUGGER_G(time_spent) > stackdriver_debugger_max_time()) {
        RETURN_FALSE;
    }

    // if we've already spent more than the time allowed, skip further breakpoints
    if (STACKDRIVER_DEBUGGER_G(memory_used) > STACKDRIVER_DEBUGGER_G(max_memory)) {
        RETURN_FALSE;
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "S", &logpoint_id) == FAILURE) {
        RETURN_FALSE;
    }

    start = stackdriver_debugger_now();
    start_memory = zend_memory_usage(0);
    logpoint = zend_hash_find_ptr(STACKDRIVER_DEBUGGER_G(logpoints_by_id), logpoint_id);

    if (logpoint == NULL || test_conditional(logpoint->condition) != SUCCESS) {
        STACKDRIVER_DEBUGGER_G(time_spent) = STACKDRIVER_DEBUGGER_G(time_spent) + stackdriver_debugger_now() - start;
        RETURN_FALSE;
    }

    evaluate_logpoint(execute_data, logpoint);
    STACKDRIVER_DEBUGGER_G(time_spent) = STACKDRIVER_DEBUGGER_G(time_spent) + stackdriver_debugger_now() - start;
    end_memory = zend_memory_usage(0);
    if (end_memory > start_memory) {
        STACKDRIVER_DEBUGGER_G(memory_used) = STACKDRIVER_DEBUGGER_G(memory_used) + end_memory - start_memory;
    }

    RETURN_TRUE;
}

/**
 * Calculate the full filename given a relative path and the current file.
 *
 * Note: The return zend_string must be released by the caller.
 */
static zend_string *stackdriver_debugger_full_filename(zend_string *relative_or_full_path, const char *source_root, int source_root_length)
{
    return strpprintf(source_root_length + 2 + ZSTR_LEN(relative_or_full_path), "%s%c%s", source_root, DEFAULT_SLASH, ZSTR_VAL(relative_or_full_path));
}

/**
 * Register a snapshot for recording.
 *
 * @param string $filename
 * @param int $line
 * @param array $options [optional] {
 *      Configuration options.
 *
 *      @type string $snapshotId Identifier for this snapshot. Defaults to a
 *            randomly generated value.
 *      @type string $condition If provided, this PHP statement will be
 *            executed at the snapshot point in that execution context. If the
 *            value is truthy, then the snapshot will be evaluated.
 *      @type array $expressions An array of additional statements to execute
 *            in the execution context that are captured along with the local
 *            variables in scope.
 *      @type string $sourceRoot Full path the the root directory of the
 *            application source code.
 *      @type callable $callback The callback to execute when the snapshot is
 *            hit.
 *      @type int $maxDepth The maximum number of stackframes whose variables
 *            are captured. If 0, then no limit. **Defaults to** 0.
 * }
 */
PHP_FUNCTION(stackdriver_debugger_add_snapshot)
{
    zend_string *filename, *full_filename, *snapshot_id = NULL, *condition = NULL, *source_root = NULL;
    zend_long lineno;
    HashTable *options = NULL, *expressions = NULL;
    zval *zv = NULL, *callback = NULL;
    zend_long max_stack_eval_depth = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Sl|h", &filename, &lineno, &options) == FAILURE) {
        RETURN_FALSE;
    }

    if (options != NULL) {
        zv = zend_hash_str_find(options, "snapshotId", strlen("snapshotId"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            snapshot_id = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "condition", strlen("condition"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            condition = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "expressions", strlen("expressions"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            expressions = Z_ARRVAL_P(zv);
        }

        zv = zend_hash_str_find(options, "sourceRoot", strlen("sourceRoot"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            source_root = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "callback", strlen("callback"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            callback = zv;
        }

        zv = zend_hash_str_find(options, "maxDepth", strlen("maxDepth"));
        if (zv != NULL && Z_TYPE_P(zv) == IS_LONG) {
            max_stack_eval_depth = Z_LVAL_P(zv);
        }
    }

    if (source_root == NULL) {
        source_root = EX(prev_execute_data)->func->op_array.filename;
        char *current_file = estrndup(ZSTR_VAL(source_root), ZSTR_LEN(source_root));
        size_t dirlen = php_dirname(current_file, ZSTR_LEN(source_root));
        full_filename = stackdriver_debugger_full_filename(filename, current_file, dirlen);
        efree(current_file);
    } else {
        full_filename = stackdriver_debugger_full_filename(filename, ZSTR_VAL(source_root), ZSTR_LEN(source_root));
    }

    if (register_snapshot(snapshot_id, full_filename, lineno, condition, expressions, callback, max_stack_eval_depth) != SUCCESS) {
        zend_string_release(full_filename);
        RETURN_FALSE;
    }

    if (stackdriver_debugger_breakpoint_injected(full_filename, snapshot_id) != SUCCESS) {
        stackdriver_debugger_opcache_invalidate(full_filename);
    }
    zend_string_release(full_filename);

    RETURN_TRUE;
}

/**
 * Register a logpoint for recording.
 *
 * @param string $filename
 * @param int $line
 * @param string $logLevel
 * @param string $format
 * @param array $options [optional] {
 *      Configuration options.
 *
 *      @type string $snapshotId
 *      @type string $condition
 *      @type array $expressions
 *      @type string $sourceRoot
 * }
 */
PHP_FUNCTION(stackdriver_debugger_add_logpoint)
{
    zend_string *filename, *full_filename, *format = NULL, *snapshot_id = NULL, *condition = NULL, *source_root = NULL, *log_level = NULL;
    zend_long lineno;
    HashTable *options = NULL, *expressions = NULL;
    zval *zv = NULL, *callback = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "SlSS|h", &filename, &lineno, &log_level, &format, &options) == FAILURE) {
        RETURN_FALSE;
    }

    if (options != NULL) {
        zv = zend_hash_str_find(options, "snapshotId", strlen("snapshotId"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            snapshot_id = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "condition", strlen("condition"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            condition = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "expressions", strlen("expressions"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            expressions = Z_ARRVAL_P(zv);
        }

        zv = zend_hash_str_find(options, "sourceRoot", strlen("sourceRoot"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            source_root = Z_STR_P(zv);
        }

        zv = zend_hash_str_find(options, "callback", strlen("callback"));
        if (zv != NULL && !Z_ISNULL_P(zv)) {
            callback = zv;
        }
    }

    if (source_root == NULL) {
        source_root = EX(prev_execute_data)->func->op_array.filename;
        char *current_file = estrndup(ZSTR_VAL(source_root), ZSTR_LEN(source_root));
        size_t dirlen = php_dirname(current_file, ZSTR_LEN(source_root));
        full_filename = stackdriver_debugger_full_filename(filename, current_file, dirlen);
        efree(current_file);
    } else {
        full_filename = stackdriver_debugger_full_filename(filename, ZSTR_VAL(source_root), ZSTR_LEN(source_root));
    }

    if (register_logpoint(snapshot_id, full_filename, lineno, log_level, condition, format, expressions, callback) != SUCCESS) {
        zend_string_release(full_filename);
        RETURN_FALSE;
    }

    if (stackdriver_debugger_breakpoint_injected(full_filename, snapshot_id) != SUCCESS) {
        stackdriver_debugger_opcache_invalidate(full_filename);
    }
    zend_string_release(full_filename);

    RETURN_TRUE;
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(stackdriver_debugger)
{
    /* allocate global request variables */
#ifdef ZTS
    ts_allocate_id(&stackdriver_debugger_globals_id, sizeof(zend_stackdriver_debugger_globals), php_stackdriver_debugger_globals_ctor, NULL);
#else
    php_stackdriver_debugger_globals_ctor(&php_stackdriver_debugger_globals_ctor);
#endif

    REGISTER_INI_ENTRIES();

    stackdriver_debugger_ast_minit(INIT_FUNC_ARGS_PASSTHRU);

    stackdriver_debugger_total_time_spent = 0.0;
    stackdriver_debugger_total_requests_handled = 0;

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(stackdriver_debugger)
{
    stackdriver_debugger_ast_mshutdown(SHUTDOWN_FUNC_ARGS_PASSTHRU);
    UNREGISTER_INI_ENTRIES();

    return SUCCESS;
}
/* }}} */

PHP_RINIT_FUNCTION(stackdriver_debugger)
{
    STACKDRIVER_DEBUGGER_G(time_spent) = 0;

    STACKDRIVER_DEBUGGER_G(request_start) = stackdriver_debugger_now();
    STACKDRIVER_DEBUGGER_G(memory_used) = 0;

    stackdriver_debugger_ast_rinit(TSRMLS_C);
    stackdriver_debugger_snapshot_rinit(TSRMLS_C);
    stackdriver_debugger_logpoint_rinit(TSRMLS_C);

    STACKDRIVER_DEBUGGER_G(opcache_enabled) = stackdriver_debugger_opcache_enabled();

    return SUCCESS;
}

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(stackdriver_debugger)
{
    stackdriver_debugger_ast_rshutdown(TSRMLS_C);
    stackdriver_debugger_snapshot_rshutdown(TSRMLS_C);
    stackdriver_debugger_logpoint_rshutdown(TSRMLS_C);

    stackdriver_debugger_total_time_spent += stackdriver_debugger_now() - STACKDRIVER_DEBUGGER_G(request_start) - STACKDRIVER_DEBUGGER_G(time_spent);
    stackdriver_debugger_total_requests_handled++;

    return SUCCESS;
}
/* }}} */


/**
 * Callback for when the user changes the max memory php.ini setting.
 */
PHP_INI_MH(OnUpdate_stackdriver_debugger_max_memory)
{
    if (new_value != NULL) {
        /* 1MB = 1024 * 1024 bytes = 1048576 bytes */
        STACKDRIVER_DEBUGGER_G(max_memory) = 1048576 * ZEND_STRTOL(ZSTR_VAL(new_value), NULL, 0);
    }
    return SUCCESS;
}
