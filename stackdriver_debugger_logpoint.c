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
#include "php_stackdriver_debugger.h"
#include "stackdriver_debugger_ast.h"
#include "stackdriver_debugger_logpoint.h"
#include "zend_exceptions.h"

#if PHP_VERSION_ID < 70100
#include "standard/php_rand.h"
#else
#include "standard/php_mt_rand.h"
#endif

#ifdef _WIN32
#include "win32/time.h"
#else
#include <sys/time.h>
#endif

#include "ext/pcre/php_pcre.h"

#if PHP_VERSION_ID < 70200
#define REGEX_REPLACE_ALL(regex, subject, replace) \
    php_pcre_replace( \
        regex, \
        subject, \
        ZSTR_VAL(subject), \
        ZSTR_LEN(subject), \
        replace, \
        0, \
        -1, \
        NULL \
    )
#else
#define REGEX_REPLACE_ALL(regex, subject, replace) \
    php_pcre_replace( \
        regex, \
        subject, \
        ZSTR_VAL(subject), \
        ZSTR_LEN(subject), \
        Z_STR_P(replace), \
        -1, \
        NULL \
    )
#endif

/* Return the current timestamp as a double */
static double stackdriver_debugger_now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (double) (tv.tv_sec + tv.tv_usec / 1000000.00);
}

/* Initialize an empty, allocated logpoint */
static void init_logpoint(stackdriver_debugger_logpoint_t *logpoint)
{
    logpoint->id = NULL;
    logpoint->filename = NULL;
    logpoint->lineno = -1;
    logpoint->condition = NULL;
    logpoint->log_level = NULL;
    logpoint->format = NULL;
    logpoint->expressions = NULL;
    logpoint->callback = NULL;
}

/* Cleanup an allocated logpoint including freeing memory */
static void destroy_logpoint(stackdriver_debugger_logpoint_t *logpoint)
{
    zend_string_release(logpoint->id);
    zend_string_release(logpoint->filename);

    if (logpoint->condition) {
        zend_string_release(logpoint->condition);
    }

    zend_string_release(logpoint->log_level);
    zend_string_release(logpoint->format);

    if (logpoint->expressions) {
        zend_hash_destroy(logpoint->expressions);
    }

    if (logpoint->callback) {
        ZVAL_PTR_DTOR(logpoint->callback);
        efree(logpoint->callback);
    }

    efree(logpoint);
}

/* Initialize an empty, allocated message */
static void init_message(stackdriver_debugger_message_t *message)
{
    message->filename = NULL;
    message->lineno = -1;
    ZVAL_NULL(&message->message);
    message->timestamp = stackdriver_debugger_now();
    message->log_level = NULL;
}

/* Cleanup an allocated message including freeing memory */
static void destroy_message(stackdriver_debugger_message_t *message)
{
    zend_string_release(message->filename);
    zend_string_release(message->log_level);
    ZVAL_DESTRUCTOR(&message->message);

    efree(message);
}

static int handle_message_callback(zval *callback, stackdriver_debugger_message_t *message)
{
    zval callback_result;
    zval args[3];
    ZVAL_STR(&args[0], message->log_level);
    args[1] = message->message;
    array_init(&args[2]);
    add_assoc_str(&args[2], "filename", message->filename);
    add_assoc_long(&args[2], "line", message->lineno);

    if (call_user_function_ex(EG(function_table), NULL, callback, &callback_result, 3, args, 0, NULL) != SUCCESS) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * Evaluate the provided logpoint in the provided executing scope.
 */
void evaluate_logpoint(zend_execute_data *execute_data, stackdriver_debugger_logpoint_t *logpoint)
{
    zval *expression;
    zend_string *m, *replaced;

    stackdriver_debugger_message_t *message = (stackdriver_debugger_message_t*)emalloc(sizeof(stackdriver_debugger_message_t));
    init_message(message);

    message->filename = zend_string_copy(logpoint->filename);
    message->lineno = logpoint->lineno;
    message->log_level = zend_string_copy(logpoint->log_level);
    m = zend_string_copy(logpoint->format);

    /* Evaluate logpoint message and store in message struct */
    if (logpoint->expressions) {
        int i;
        ZEND_HASH_FOREACH_NUM_KEY_VAL(logpoint->expressions, i, expression) {
            zval retval;

            if (zend_eval_string(Z_STRVAL_P(expression), &retval, "expression evaluation") == SUCCESS) {
                convert_to_string(&retval);

                zend_string *regex = strpprintf(sizeof("/(?<!\\$)\\$/") + 2, "/(?<!\\$)\\$%d/", i);
                replaced = REGEX_REPLACE_ALL(regex, m, &retval);
                zend_string_release(m);
                zend_string_release(regex);
                m = replaced;
            }
        } ZEND_HASH_FOREACH_END();
    }
    ZVAL_STR(&message->message, m);

    if (logpoint->callback) {
        if (handle_message_callback(logpoint->callback, message) != SUCCESS) {
            php_error_docref(NULL, E_WARNING, "Error running logpoint callback.");
        }
        if (EG(exception) != NULL) {
            zend_clear_exception();
            php_error_docref(NULL, E_WARNING, "Error running logpoint callback.");
        }
        destroy_message(message);
    } else {
        zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(collected_messages), message);
    }
}

/**
 * Registers a logpoint for recording. We store the logpoint configuration in a
 * request global HashTable by file which is consulted during file compilation.
 */
int register_logpoint(zend_string *logpoint_id, zend_string *filename,
    zend_long lineno, zend_string *log_level, zend_string *condition,
    zend_string *format, HashTable *expressions, zval *callback)
{
    zval *logpoints, *logpoint_ptr;
    stackdriver_debugger_logpoint_t *logpoint;

    PHP_STACKDRIVER_DEBUGGER_MAKE_STD_ZVAL(logpoint_ptr);
    logpoint = emalloc(sizeof(stackdriver_debugger_logpoint_t));
    init_logpoint(logpoint);

    if (logpoint_id == NULL) {
        #if PHP_VERSION_ID < 70100
            if (!BG(mt_rand_is_seeded)) {
                php_mt_srand(GENERATE_SEED());
            }
        #endif
        logpoint->id = strpprintf(20, "%d", php_mt_rand());
    } else {
        logpoint->id = zend_string_copy(logpoint_id);
    }
    logpoint->filename = zend_string_copy(filename);
    logpoint->lineno = lineno;
    logpoint->format = zend_string_copy(format);
    logpoint->log_level = zend_string_copy(log_level);
    if (condition != NULL && ZSTR_LEN(condition) > 0) {
        if (valid_debugger_statement(condition) != SUCCESS) {
            return FAILURE;
        }

        logpoint->condition = zend_string_copy(condition);
    }
    if (expressions != NULL) {
        zval *expression;

        ALLOC_HASHTABLE(logpoint->expressions);
        zend_hash_init(logpoint->expressions, expressions->nNumUsed, NULL, ZVAL_PTR_DTOR, 0);

        ZEND_HASH_FOREACH_VAL(expressions, expression) {
            if (valid_debugger_statement(Z_STR_P(expression)) != SUCCESS) {
                return FAILURE;
            }
            zend_hash_next_index_insert(logpoint->expressions, expression);
        } ZEND_HASH_FOREACH_END();
    }
    if (callback != NULL) {
        logpoint->callback = (zval *)(emalloc(sizeof(zval)));
        ZVAL_DUP(logpoint->callback, callback);
    }

    ZVAL_PTR(logpoint_ptr, logpoint);

    logpoints = zend_hash_find(STACKDRIVER_DEBUGGER_G(logpoints_by_file), filename);
    if (logpoints == NULL) {
        /* initialize logpoints as array */
        PHP_STACKDRIVER_DEBUGGER_MAKE_STD_ZVAL(logpoints);
        array_init(logpoints);
    }

    add_next_index_zval(logpoints, logpoint_ptr);

    zend_hash_update(STACKDRIVER_DEBUGGER_G(logpoints_by_file), filename, logpoints);
    zend_hash_update(STACKDRIVER_DEBUGGER_G(logpoints_by_id), logpoint->id, logpoint_ptr);

    return SUCCESS;
}

/**
 * Fetch the list of collected messgase and return as an array of data
 */
void list_logpoints(zval *return_value)
{
    stackdriver_debugger_message_t *message;
    ZEND_HASH_FOREACH_PTR(STACKDRIVER_DEBUGGER_G(collected_messages), message) {
        zval zmessage;
        array_init(&zmessage);

        add_assoc_str(&zmessage, "filename", message->filename);
        add_assoc_long(&zmessage, "line", message->lineno);
        add_assoc_zval(&zmessage, "message", &message->message);
        add_assoc_long(&zmessage, "timestamp", message->timestamp);
        add_assoc_str(&zmessage, "level", message->log_level);
        add_next_index_zval(return_value, &zmessage);
    } ZEND_HASH_FOREACH_END();
}

/**
 * Destructor for cleaning up a zval pointer which contains a manually
 * emalloc'ed logpoint pointer. This should efree all manually emalloc'ed data
 * within the logpoint and also call the zval's normal destructor as well.
 */
static void logpoint_dtor(zval *zv)
{
    stackdriver_debugger_logpoint_t *logpoint = (stackdriver_debugger_logpoint_t *)Z_PTR_P(zv);
    destroy_logpoint(logpoint);
    ZVAL_PTR_DTOR(zv);
}

/**
 * Destructor for cleaning up a zval pointer which contains a manually
 * emalloc'ed message pointer. This should efree all manually emalloc'ed data
 * within the message and also call the zval's normal destructor as well.
 */
static void message_dtor(zval *zv)
{
    stackdriver_debugger_message_t *message = (stackdriver_debugger_message_t *)Z_PTR_P(zv);
    destroy_message(message);
    ZVAL_PTR_DTOR(zv);
}

/**
 * Request initialization lifecycle hook. Initializes request global variables.
 */
int stackdriver_debugger_logpoint_rinit(TSRMLS_D)
{
    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(logpoints_by_id));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(logpoints_by_id), 16, NULL, logpoint_dtor, 0);

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(logpoints_by_file));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(logpoints_by_file), 16, NULL, ZVAL_PTR_DTOR, 0);

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(collected_messages));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(collected_messages), 16, NULL, message_dtor, 0);

    return SUCCESS;
}

/**
 * Request shutdown lifecycle hook. Destroys request global variables.
 */
int stackdriver_debugger_logpoint_rshutdown(TSRMLS_D)
{
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(collected_messages));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(logpoints_by_file));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(logpoints_by_id));
    return SUCCESS;
}
