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
#include "stackdriver_debugger_random.h"
#include "spl/php_spl.h"

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

    efree(variable);
}

static void stackframe_locals_dtor(zval *zv)
{
    stackdriver_debugger_variable_t *variable = (stackdriver_debugger_variable_t *)Z_PTR_P(zv);
    destroy_variable(variable);
    ZVAL_PTR_DTOR(zv);
}

/* Initialize an empty, allocated stackframe */
static void init_stackframe(stackdriver_debugger_stackframe_t *stackframe)
{
    stackframe->function = NULL;
    stackframe->filename = NULL;
    stackframe->lineno = -1;
    ALLOC_HASHTABLE(stackframe->locals);
    zend_hash_init(stackframe->locals, 16, NULL, stackframe_locals_dtor, 0);
}

/* Cleanup an allocated stackframe including freeing memory */
static void destroy_stackframe(stackdriver_debugger_stackframe_t *stackframe)
{
    int i;

    if (stackframe->function && (int)stackframe->function != -1) {
        zend_string_release(stackframe->function);
    }

    if (stackframe->filename) {
        zend_string_release(stackframe->filename);
    }

    zend_hash_destroy(stackframe->locals);
    FREE_HASHTABLE(stackframe->locals);

    efree(stackframe);
}

static void stackframes_dtor(zval *zv)
{
    stackdriver_debugger_stackframe_t *sf = (stackdriver_debugger_stackframe_t *)Z_PTR_P(zv);
    destroy_stackframe(sf);
    ZVAL_PTR_DTOR(zv);
}

/* Initialize an empty, allocated snapshot */
static void init_snapshot(stackdriver_debugger_snapshot_t *snapshot)
{
    snapshot->id = NULL;
    snapshot->filename = NULL;
    snapshot->lineno = -1;
    snapshot->condition = NULL;
    snapshot->fulfilled = 0;
    ALLOC_HASHTABLE(snapshot->expressions);
    zend_hash_init(snapshot->expressions, 16, NULL, ZVAL_PTR_DTOR, 0);
    ALLOC_HASHTABLE(snapshot->evaluated_expressions);
    zend_hash_init(snapshot->evaluated_expressions, 16, NULL, ZVAL_PTR_DTOR, 0);
    ALLOC_HASHTABLE(snapshot->stackframes);
    zend_hash_init(snapshot->stackframes, 16, NULL, stackframes_dtor, 0);
    ZVAL_NULL(&snapshot->callback);
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

    zend_hash_destroy(snapshot->expressions);
    FREE_HASHTABLE(snapshot->expressions);

    zend_hash_destroy(snapshot->evaluated_expressions);
    FREE_HASHTABLE(snapshot->evaluated_expressions);

    zend_hash_destroy(snapshot->stackframes);
    FREE_HASHTABLE(snapshot->stackframes);

    if (Z_TYPE(snapshot->callback) != IS_NULL) {
        zval_ptr_dtor(&snapshot->callback);
    }
    efree(snapshot);
}

/**
 * Convert a collected stackdriver_debugger_variable_t into an array zval with
 * keys of name and value.
 */
static void variable_to_zval(zval *return_value, stackdriver_debugger_variable_t *variable)
{
    zend_string *hash = NULL;
    array_init(return_value);
    add_assoc_str(return_value, "name", variable->name);
    add_assoc_zval(return_value, "value", &variable->value);
    switch (Z_TYPE(variable->value)) {
        case IS_OBJECT:
            /* Use the spl_object_hash value */
            hash = php_spl_object_hash(&variable->value);
            break;
        case IS_ARRAY:
            /* Use the memory address of the zend_array */
            hash = strpprintf(16, "%016zx", Z_ARR(variable->value));
            break;
        case IS_STRING:
            /* Use the internal HashTable value */
            hash = strpprintf(32, "%016zx", ZSTR_HASH(Z_STR(variable->value)));
            break;
    }
    if (hash != NULL) {
        add_assoc_str(return_value, "id", hash);
    }
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
        add_assoc_str(return_value, "function", zend_string_dup(stackframe->function, 0));
    }
    add_assoc_str(return_value, "filename", zend_string_copy(stackframe->filename));
    add_assoc_long(return_value, "line", stackframe->lineno);

    zval locals;
    array_init(&locals);
    stackdriver_debugger_variable_t *local_variable;
    ZEND_HASH_FOREACH_PTR(stackframe->locals, local_variable) {
        zval local;
        variable_to_zval(&local, local_variable);
        add_next_index_zval(&locals, &local);
    } ZEND_HASH_FOREACH_END();

    add_assoc_zval(return_value, "locals", &locals);
}

/**
 * Convert a collected stackdriver_debugger_snapshot_t into an array zval
 * of arrays converted from the contained stackframes
 */
static void stackframes_to_zval(zval *return_value, stackdriver_debugger_snapshot_t *snapshot)
{
    int i;
    array_init(return_value);
    stackdriver_debugger_stackframe_t *stackframe;

    ZEND_HASH_FOREACH_PTR(snapshot->stackframes, stackframe) {
        zval zstackframe;
        stackframe_to_zval(&zstackframe, stackframe);
        add_next_index_zval(return_value, &zstackframe);
    } ZEND_HASH_FOREACH_END();
}

/**
 * Convert a collected evaluated_expressions into an array zval of values.
 */
static void expressions_to_zval(zval *return_value, stackdriver_debugger_snapshot_t *snapshot)
{
    array_init(return_value);
    zend_hash_copy(Z_ARR_P(return_value), snapshot->evaluated_expressions, NULL);
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

    ZEND_HASH_FOREACH_STR_KEY_VAL(symbol_table, name, value) {
        stackdriver_debugger_variable_t *local = create_variable(name, value);
        zend_hash_next_index_insert_ptr(stackframe->locals, local);
    } ZEND_HASH_FOREACH_END();

    /* Free symbol table if necessary (potential memory leak) */
    if (allocated != 0) {
        zend_hash_destroy(symbol_table);
        FREE_HASHTABLE(symbol_table);
    }
}

/**
 * Capture the execution state from `execute_data`
 */
static stackdriver_debugger_stackframe_t *execute_data_to_stackframe(zend_execute_data *execute_data, int capture_variables)
{
    stackdriver_debugger_stackframe_t *stackframe;
    zend_op_array *op_array;
    zend_string *funcname;

    if (!execute_data->func || !ZEND_USER_CODE(execute_data->func->common.type)) {
        return NULL;
    }
    stackframe = (stackdriver_debugger_stackframe_t *)emalloc(sizeof(stackdriver_debugger_stackframe_t));
    init_stackframe(stackframe);

    op_array = &execute_data->func->op_array;
    funcname = op_array->function_name;

    stackframe->function = NULL;
    if (funcname != NULL) {
        stackframe->function = zend_string_copy(funcname);
    }
    stackframe->filename = zend_string_copy(op_array->filename);
    stackframe->lineno = execute_data->opline->lineno;

    if (capture_variables == 1) {
        capture_locals(execute_data, stackframe);
    }

    return stackframe;
}

/**
 * Registers a snapshot for recording. We store the snapshot configuration in a
 * request global HashTable by file which is consulted during file compilation.
 */
int register_snapshot(zend_string *snapshot_id, zend_string *filename,
    zend_long lineno, zend_string *condition, HashTable *expressions,
    zval *callback, zend_long max_stack_eval_depth)
{
    HashTable *snapshots;
    stackdriver_debugger_snapshot_t *snapshot;

    snapshot = emalloc(sizeof(stackdriver_debugger_snapshot_t));
    init_snapshot(snapshot);

    if (snapshot_id == NULL) {
        snapshot->id = generate_breakpoint_id();
    } else {
        snapshot->id = zend_string_copy(snapshot_id);
    }
    snapshot->filename = zend_string_copy(filename);
    snapshot->lineno = lineno;
    snapshot->max_stack_eval_depth = max_stack_eval_depth;
    if (condition != NULL && ZSTR_LEN(condition) > 0) {
        if (valid_debugger_statement(condition) != SUCCESS) {
            destroy_snapshot(snapshot);
            return FAILURE;
        }

        snapshot->condition = zend_string_copy(condition);
    }
    if (expressions != NULL) {
        zval *expression;

        ZEND_HASH_FOREACH_VAL(expressions, expression) {
            if (valid_debugger_statement(Z_STR_P(expression)) != SUCCESS) {
                destroy_snapshot(snapshot);
                return FAILURE;
            }
            zend_hash_next_index_insert(snapshot->expressions, expression);
        } ZEND_HASH_FOREACH_END();
    }
    if (callback != NULL) {
        ZVAL_COPY(&snapshot->callback, callback);
    }

    snapshots = zend_hash_find_ptr(STACKDRIVER_DEBUGGER_G(snapshots_by_file), filename);
    if (snapshots == NULL) {
        ALLOC_HASHTABLE(snapshots);
        zend_hash_init(snapshots, 4, NULL, ZVAL_PTR_DTOR, 0);
        zend_hash_update_ptr(STACKDRIVER_DEBUGGER_G(snapshots_by_file), filename, snapshots);
    }

    zend_hash_next_index_insert_ptr(snapshots, snapshot);
    zend_hash_update_ptr(STACKDRIVER_DEBUGGER_G(snapshots_by_id), snapshot->id, snapshot);

    return SUCCESS;
}

/**
 * Capture the full execution state into the provided snapshot
 */
static void capture_execution_state(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot)
{
    zend_execute_data *ptr = execute_data;
    stackdriver_debugger_stackframe_t *stackframe;
    int i = 0;

    while (ptr) {
        if (snapshot->max_stack_eval_depth == 0 || i < snapshot->max_stack_eval_depth) {
            stackframe = execute_data_to_stackframe(ptr, 1);
        } else {
            stackframe = execute_data_to_stackframe(ptr, 0);
        }
        if (stackframe != NULL) {
            zend_hash_next_index_insert_ptr(snapshot->stackframes, stackframe);
            i++;
        }
        ptr = ptr->prev_execute_data;
    }
}

/**
 * Evaluate each provided expression and capture the result into the
 * provided snapshot data struct.
 */
static void capture_expressions(zend_execute_data *execute_data, stackdriver_debugger_snapshot_t *snapshot)
{
    zval *expression;

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

static int handle_snapshot_callback(zval *callback, stackdriver_debugger_snapshot_t *snapshot)
{
    zval zsnapshot, callback_result;
    snapshot_to_zval(&zsnapshot, snapshot);
    int call_result = call_user_function_ex(EG(function_table), NULL, callback, &callback_result, 1, &zsnapshot, 0, NULL);

    zval_ptr_dtor(&zsnapshot);
    zval_ptr_dtor(&callback_result);
    return call_result;
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
    if (Z_TYPE(snapshot->callback) != IS_NULL) {
        if (handle_snapshot_callback(&snapshot->callback, snapshot) != SUCCESS) {
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

static void snapshots_by_file_dtor(zval *zv)
{
    HashTable *ht = (HashTable *)Z_PTR_P(zv);
    zend_hash_destroy(ht);
    FREE_HASHTABLE(ht);
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
    zend_hash_init(STACKDRIVER_DEBUGGER_G(snapshots_by_file), 16, NULL, snapshots_by_file_dtor, 0);

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
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(collected_snapshots_by_id));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(snapshots_by_file));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(snapshots_by_file));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(snapshots_by_id));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(snapshots_by_id));

    return SUCCESS;
}
