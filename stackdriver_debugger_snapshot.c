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
#include "stackdriver_debugger_snapshot.h"
#include "zend_exceptions.h"

#if PHP_VERSION_ID < 70100
#include "standard/php_rand.h"
#else
#include "standard/php_mt_rand.h"
#endif

/* Initialize an empty, allocated variable */
static void init_variable(stackdriver_debugger_variable_t *variable)
{
    variable->name = NULL;
    ZVAL_NULL(&variable->value);
    variable->indirect = 0;
}

/* Cleanup an allocated variable including freeing memory */
static void destroy_variable(stackdriver_debugger_variable_t *variable)
{
    if (variable->name) {
        zend_string_release(variable->name);
    }

    ZVAL_PTR_DTOR(&variable->value);

    efree(variable);
}

/* Initialize an empty, allocated stackframe */
static void init_stackframe(stackdriver_debugger_stackframe_t *stackframe)
{
    stackframe->function = NULL;
    stackframe->filename = NULL;
    stackframe->lineno = -1;
    stackframe->locals_count = 0;
    stackframe->locals = NULL;
}

/* Cleanup an allocated stackframe including freeing memory */
static void destroy_stackframe(stackdriver_debugger_stackframe_t *stackframe)
{
    int i;

    if (stackframe->function) {
        zend_string_release(stackframe->function);
    }

    if (stackframe->filename) {
        zend_string_release(stackframe->filename);
    }

    for (i = 0; i < stackframe->locals_count; i ++) {
        destroy_variable(stackframe->locals[i]);
    }
    efree(stackframe);
}

/* Initialize an empty, allocated snapshot */
static void init_snapshot(stackdriver_debugger_snapshot_t *snapshot)
{
    snapshot->id = NULL;
    snapshot->filename = NULL;
    snapshot->lineno = -1;
    snapshot->condition = NULL;
    snapshot->fulfilled = 0;
    snapshot->expressions = NULL;
    snapshot->evaluated_expressions = NULL;
    snapshot->stackframes_count = 0;
    snapshot->stackframes = NULL;
    snapshot->callback = NULL;
}

/* Cleanup an allocated snapshot including freeing memory */
static void destroy_snapshot(stackdriver_debugger_snapshot_t *snapshot)
{
    int i;

    zend_string_release(snapshot->id);
    zend_string_release(snapshot->filename);

    if (snapshot->condition) {
        zend_string_release(snapshot->condition);
    }

    if (snapshot->expressions) {
        zend_hash_destroy(snapshot->expressions);
    }

    if (snapshot->evaluated_expressions) {
        zend_hash_destroy(snapshot->evaluated_expressions);
    }

    if (snapshot->callback) {
        ZVAL_PTR_DTOR(snapshot->callback);
        efree(snapshot->callback);
    }

    for (i = 0; i < snapshot->stackframes_count; i++) {
        destroy_stackframe(snapshot->stackframes[i]);
    }
    efree(snapshot);
}

/**
 * Convert a collected stackdriver_debugger_variable_t into an array zval with
 * keys of name and value.
 */
static void variable_to_zval(zval *return_value, stackdriver_debugger_variable_t *variable)
{
    array_init(return_value);
    add_assoc_str(return_value, "name", variable->name);
    add_assoc_zval(return_value, "value", &variable->value);
}

/* Capture a variable with provided name and zval into a collected variable */
static stackdriver_debugger_variable_t *create_variable(zend_string *name, zval *zv)
{
    stackdriver_debugger_variable_t *variable = (stackdriver_debugger_variable_t *)emalloc(sizeof(stackdriver_debugger_variable_t));
    init_variable(variable);
    variable->name = zend_string_dup(name, 0);

    /* If the zval is an indirect, dereference it */
    while (Z_TYPE_P(zv) == IS_INDIRECT) {
        variable->indirect = 1;
        zv = Z_INDIRECT_P(zv);
    }

    ZVAL_COPY(&variable->value, zv);

    return variable;
}

/**
 * Convert a collected stackdriver_debugger_stackframe_t into an array zval
 * with function, filename, line, and locals.
 */
static void stackframe_to_zval(zval *return_value, stackdriver_debugger_stackframe_t *stackframe)
{
    int i;
    array_init(return_value);

    if (stackframe->function) {
        add_assoc_str(return_value, "function", stackframe->function);
    }
    add_assoc_str(return_value, "filename", stackframe->filename);
    add_assoc_long(return_value, "line", stackframe->lineno);
    if (stackframe->locals_count) {
        zval locals, local;
        array_init(&locals);
        for (i = 0; i < stackframe->locals_count; i++) {
            variable_to_zval(&local, stackframe->locals[i]);
            add_next_index_zval(&locals, &local);
        }

        add_assoc_zval(return_value, "locals", &locals);
    }
}

/**
 * Convert a collected stackdriver_debugger_snapshot_t into an array zval
 * of arrays converted from the contained stackframes
 */
static void stackframes_to_zval(zval *return_value, stackdriver_debugger_snapshot_t *snapshot)
{
    int i;
    array_init(return_value);

    for (i = 0; i < snapshot->stackframes_count; i++) {
        zval stackframe;
        stackframe_to_zval(&stackframe, snapshot->stackframes[i]);
        add_next_index_zval(return_value, &stackframe);
    }
}

/**
 * Convert a collected evaluated_expressions into an array zval of values.
 */
static void expressions_to_zval(zval *return_value, stackdriver_debugger_snapshot_t *snapshot)
{
    if (snapshot->evaluated_expressions == NULL) {
        array_init(return_value);
        return;
    }

    ZVAL_ARR(return_value, snapshot->evaluated_expressions);
    Z_TRY_ADDREF_P(return_value);
}

static void snapshot_to_zval(zval *return_value, stackdriver_debugger_snapshot_t *snapshot)
{
    zval zstackframes, zexpressions;
    array_init(return_value);
    stackframes_to_zval(&zstackframes, snapshot);
    expressions_to_zval(&zexpressions, snapshot);

    add_assoc_str(return_value, "id", snapshot->id);
    add_assoc_zval(return_value, "stackframes", &zstackframes);
    add_assoc_zval(return_value, "evaluatedExpressions", &zexpressions);
}

/**
 * Given a provided zend_execute_data, find or rebuild the HashTable of
 * local variables at that moment.
 */
static int execute_data_to_symbol_table(zend_execute_data *execute_data, zend_array **symbol_table_p)
{
    zend_op_array *op_array = &execute_data->func->op_array;
#if PHP_VERSION_ID < 70100
    if (execute_data->symbol_table) {
#else
    if (ZEND_CALL_INFO(execute_data) & ZEND_CALL_HAS_SYMBOL_TABLE) {
#endif
        *symbol_table_p = execute_data->symbol_table;
        return 0;
    } else {
        int i;
        zend_array *symbol_table;
        ALLOC_HASHTABLE(symbol_table);
        zend_hash_init(symbol_table, op_array->last_var, NULL, ZVAL_PTR_DTOR, 0);
        zend_string *name;

        for (i = 0; i < op_array->last_var; i++) {
            zval copy, *var;
            name = op_array->vars[i];
            var = ZEND_CALL_VAR_NUM(execute_data, i);
            if (Z_TYPE_P(var) == IS_UNDEF) {
                ZVAL_NULL(&copy);
            } else {
                ZVAL_COPY(&copy, ZEND_CALL_VAR_NUM(execute_data, i));
            }
            zend_hash_add(symbol_table, name, &copy);
        }

        *symbol_table_p = symbol_table;
        return 1;
    }
}

/**
 * Capture all local variables at the given execution scope from `execute_data`
 * into the provided stackframe struct.
 */
static void capture_locals(zend_execute_data *execute_data, stackdriver_debugger_stackframe_t *stackframe)
{
    zend_array *symbol_table;
    zend_string *name;
    zval *value;
    int i = 0;
    int allocated = execute_data_to_symbol_table(execute_data, &symbol_table);

    stackframe->locals_count = symbol_table->nNumUsed;
    stackframe->locals = emalloc(stackframe->locals_count * sizeof(stackdriver_debugger_variable_t*));

    ZEND_HASH_FOREACH_STR_KEY_VAL(symbol_table, name, value) {
        stackframe->locals[i++] = create_variable(name, value);
    } ZEND_HASH_FOREACH_END();

    /* Free symbol table if necessary (potential memory leak) */
    if (allocated != 0) {
        zend_hash_destroy(symbol_table);
    }
}

/**
 * Capture the execution state from `execute_data`
 */
static int execute_data_to_stackframe(zend_execute_data *execute_data, stackdriver_debugger_stackframe_t **stackframe_p)
{
    stackdriver_debugger_stackframe_t *stackframe = *stackframe_p;
    zend_op_array *op_array;
    zend_string *funcname;

    if (!execute_data->func || !ZEND_USER_CODE(execute_data->func->common.type)) {
        return FAILURE;
    }
    op_array = &execute_data->func->op_array;
    funcname = op_array->function_name;

    stackframe->function = NULL;
    if (funcname != NULL) {
        stackframe->function = zend_string_copy(funcname);
    }
    stackframe->filename = zend_string_copy(op_array->filename);
    stackframe->lineno = execute_data->opline->lineno;

    capture_locals(execute_data, stackframe);

    return SUCCESS;
}

/**
 * Registers a snapshot for recording. We store the snapshot configuration in a
 * request global HashTable by file which is consulted during file compilation.
 */
int register_snapshot(zend_string *snapshot_id, zend_string *filename,
    zend_long lineno, zend_string *condition, HashTable *expressions,
    zval *callback)
{
    zval *snapshots, *snapshot_ptr;
    stackdriver_debugger_snapshot_t *snapshot;

    PHP_STACKDRIVER_DEBUGGER_MAKE_STD_ZVAL(snapshot_ptr);
    snapshot = emalloc(sizeof(stackdriver_debugger_snapshot_t));
    init_snapshot(snapshot);

    if (snapshot_id == NULL) {
        #if PHP_VERSION_ID < 70100
            if (!BG(mt_rand_is_seeded)) {
                php_mt_srand(GENERATE_SEED());
            }
        #endif
        snapshot->id = strpprintf(32, "%d", php_mt_rand());
    } else {
        snapshot->id = zend_string_copy(snapshot_id);
    }
    snapshot->filename = zend_string_copy(filename);
    snapshot->lineno = lineno;
    if (condition != NULL && ZSTR_LEN(condition) > 0) {
        if (valid_debugger_statement(condition) != SUCCESS) {
            return FAILURE;
        }

        snapshot->condition = zend_string_copy(condition);
    }
    if (expressions != NULL) {
        zval *expression;

        ALLOC_HASHTABLE(snapshot->expressions);
        zend_hash_init(snapshot->expressions, expressions->nNumUsed, NULL, ZVAL_PTR_DTOR, 0);

        ZEND_HASH_FOREACH_VAL(expressions, expression) {
            if (valid_debugger_statement(Z_STR_P(expression)) != SUCCESS) {
                return FAILURE;
            }
            zend_hash_next_index_insert(snapshot->expressions, expression);
        } ZEND_HASH_FOREACH_END();
    }
    if (callback != NULL) {
        snapshot->callback = (zval *)(emalloc(sizeof(zval)));
        ZVAL_DUP(snapshot->callback, callback);
    }

    ZVAL_PTR(snapshot_ptr, snapshot);

    snapshots = zend_hash_find(STACKDRIVER_DEBUGGER_G(snapshots_by_file), filename);
    if (snapshots == NULL) {
        /* initialize snapshots as array */
        PHP_STACKDRIVER_DEBUGGER_MAKE_STD_ZVAL(snapshots);
        array_init(snapshots);
    }

    add_next_index_zval(snapshots, snapshot_ptr);

    zend_hash_update(STACKDRIVER_DEBUGGER_G(snapshots_by_file), filename, snapshots);
    zend_hash_update(STACKDRIVER_DEBUGGER_G(snapshots_by_id), snapshot->id, snapshot_ptr);

    return SUCCESS;
}

/**
 * Capture the full execution state into the provided snapshot
 */
static void capture_execution_state(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot)
{
    zend_execute_data *ptr = execute_data;
    HashTable *backtrace;
    stackdriver_debugger_stackframe_t *stackframe;
    int i = 0;

    ALLOC_HASHTABLE(backtrace);
    zend_hash_init(backtrace, 16, NULL, ZVAL_PTR_DTOR, 0);

    while (ptr) {
        stackframe = (stackdriver_debugger_stackframe_t *)emalloc(sizeof(stackdriver_debugger_stackframe_t));
        if (execute_data_to_stackframe(ptr, &stackframe) == SUCCESS) {
            zend_hash_next_index_insert_ptr(backtrace, stackframe);
        }
        ptr = ptr->prev_execute_data;
    }

    snapshot->stackframes_count = backtrace->nNumUsed;
    snapshot->stackframes = emalloc(snapshot->stackframes_count * sizeof(stackdriver_debugger_stackframe_t*));

    ZEND_HASH_FOREACH_NUM_KEY_PTR(backtrace, i, stackframe) {
        snapshot->stackframes[i] = stackframe;
    } ZEND_HASH_FOREACH_END();

    zend_hash_destroy(backtrace);
}

/**
 * Evaluate each provided expression and capture the result into the
 * provided snapshot data struct.
 */
static void capture_expressions(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot)
{
    zval *expression;

    if (snapshot->expressions) {
        ALLOC_HASHTABLE(snapshot->evaluated_expressions);
        zend_hash_init(snapshot->evaluated_expressions, snapshot->expressions->nNumUsed, NULL, ZVAL_PTR_DTOR, 0);
        ZEND_HASH_FOREACH_VAL(snapshot->expressions, expression) {
            zval retval;

            if (zend_eval_string(Z_STRVAL_P(expression), &retval, "expression evaluation") == SUCCESS) {
                zend_hash_add(snapshot->evaluated_expressions, Z_STR_P(expression), &retval);
            } else {
                ZVAL_STRING(&retval, "ERROR");
                zend_hash_add(snapshot->evaluated_expressions, Z_STR_P(expression), &retval);
            }
        } ZEND_HASH_FOREACH_END();
    }
}

static int handle_snapshot_callback(zval *callback, stackdriver_debugger_snapshot_t *snapshot)
{
    zval zsnapshot, callback_result;
    snapshot_to_zval(&zsnapshot, snapshot);
    if (call_user_function_ex(EG(function_table), NULL, callback, &callback_result, 1, &zsnapshot, 0, NULL) != SUCCESS) {
        return FAILURE;
    }
    return SUCCESS;
}

/**
 * Evaluate the provided snapshot in the provided execution scope.
 */
void evaluate_snapshot(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot)
{
    if (snapshot->fulfilled) {
        return;
    }
    snapshot->fulfilled = 1;

    /* collect locals at each level of the backtrace */
    capture_execution_state(execute_data, snapshot);

    /* evaluate and collect expressions */
    capture_expressions(execute_data, snapshot);

    /* record as collected */
    if (snapshot->callback) {
        if (handle_snapshot_callback(snapshot->callback, snapshot) != SUCCESS) {
            php_error_docref(NULL, E_WARNING, "Error running snapshot callback.");
        }
        if (EG(exception) != NULL) {
            zend_clear_exception();
            php_error_docref(NULL, E_WARNING, "Error running snapshot callback.");
        }
    } else {
        zend_hash_update_ptr(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id), snapshot->id, snapshot);
    }
}

/**
 * Fetch the list of collected snapshots and return as an array of data
 */
void list_snapshots(zval *return_value)
{
    stackdriver_debugger_snapshot_t *snapshot;
    ZEND_HASH_FOREACH_PTR(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id), snapshot) {
        zval zsnapshot;
        snapshot_to_zval(&zsnapshot, snapshot);
        add_next_index_zval(return_value, &zsnapshot);
    } ZEND_HASH_FOREACH_END();
}

/**
 * Destructor for cleaning up a zval pointer which contains a manually
 * emalloc'ed snapshot pointer. This should efree all manually emalloc'ed data
 * within the snapshot and also call the zval's normal destructor as well.
 */
static void snapshot_dtor(zval *zv)
{
    stackdriver_debugger_snapshot_t *snapshot = (stackdriver_debugger_snapshot_t *)Z_PTR_P(zv);
    destroy_snapshot(snapshot);
    ZVAL_PTR_DTOR(zv);
}

/**
 * Request initialization lifecycle hook. Initializes request global variables.
 */
int stackdriver_debugger_snapshot_rinit(TSRMLS_D)
{
    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(snapshots_by_id));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(snapshots_by_id), 16, NULL, snapshot_dtor, 0);

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(snapshots_by_file));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(snapshots_by_file), 16, NULL, ZVAL_PTR_DTOR, 0);

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id), 16, NULL, ZVAL_PTR_DTOR, 0);

    return SUCCESS;
}

/**
 * Request shutdown lifecycle hook. Destroys request global variables.
 */
int stackdriver_debugger_snapshot_rshutdown(TSRMLS_D)
{
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(snapshots_by_file));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(snapshots_by_id));

    return SUCCESS;
}
