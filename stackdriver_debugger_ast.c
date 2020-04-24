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

#include "php_stackdriver_debugger.h"
#include "stackdriver_debugger_ast.h"
#include "stackdriver_debugger_snapshot.h"
#include "stackdriver_debugger_logpoint.h"
#include "zend_language_scanner.h"
#include "zend_exceptions.h"
#include "main/php_ini.h"
#include "zend_ini.h"

/* True global for storing the original zend_ast_process */
static void (*original_zend_ast_process)(zend_ast*);

/* map of function name -> empty null zval */
static HashTable global_whitelisted_functions;

/* map of filename -> (map of breakpoint id -> nil) */
static HashTable registered_breakpoints;

/**
 * This method generates a new abstract syntax tree that injects a function
 * call to `stackdriver_debugger()`.
 * Format:
 *
 *   ZEND_AST_STMT_LIST
 *   - ZEND_AST_CALL
 *     - ZEND_AST_ZVAL (string, the callback function)
 *     - ZEND_AST_ARG_LIST (empty list)
 *       - ZEND_AST_ZVAL (string, the breakpoint id)
 *   - original zend_ast node
 *
 * Note: we are emalloc-ing memory here, but it is expected that the PHP
 * internals recursively walk the syntax tree and free allocated memory. This
 * method cannot leave dangling pointers or the allocated memory may never be
 * freed.
 *
 * Note: you also need to set the second child of this result when injecting
 * to execute the statement you are replacing.
 */
static zend_ast_list *create_debugger_ast(const char *callback, zend_string *breakpoint_id, uint32_t lineno)
{
    zend_ast *new_call;
    zend_ast_zval *var, *snapshot_id;
    zend_ast_list *new_list, *arg_list;

    var = emalloc(sizeof(zend_ast_zval));
    var->kind = ZEND_AST_ZVAL;
    ZVAL_STRING(&var->val, callback);
    var->val.u2.lineno = lineno;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), var);

    snapshot_id = emalloc(sizeof(zend_ast_zval));
    snapshot_id->kind = ZEND_AST_ZVAL;
    ZVAL_STR(&snapshot_id->val, breakpoint_id);
    snapshot_id->val.u2.lineno = lineno;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), snapshot_id);

    arg_list = emalloc(sizeof(zend_ast_list) + sizeof(zend_ast*));
    arg_list->kind = ZEND_AST_ARG_LIST;
    arg_list->lineno = lineno;
    arg_list->children = 1;
    arg_list->child[0] = (zend_ast*)snapshot_id;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), arg_list);

    new_call = emalloc(sizeof(zend_ast) + sizeof(zend_ast*));
    new_call->kind = ZEND_AST_CALL;
    new_call->lineno = lineno;
    new_call->child[0] = (zend_ast*)var;
    new_call->child[1] = (zend_ast*)arg_list;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), new_call);

    /* create a new statement list */
    new_list = emalloc(sizeof(zend_ast_list) + sizeof(zend_ast*));
    new_list->kind = ZEND_AST_STMT_LIST;
    new_list->lineno = lineno;
    new_list->children = 2;
    new_list->child[0] = new_call;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), new_list);

    return new_list;
}

/**
 * This method walks through the AST looking for the last non-list statement to
 * replace with a new AST node that first calls the `stackdriver_debugger()`
 * function, then calls the original statement.
 *
 * This function returns SUCCESS if we have already injected into the syntax
 * tree. Otherwise, the function returns FAILURE.
 */
static int inject_ast(zend_ast *ast, zend_ast_list *to_insert)
{
    int i, num_children;
    zend_ast *current;
    zend_ast_list *list;
    zend_ast_decl *decl;
    zend_ast_zval *azval;

    if (ast == NULL) {
        return FAILURE;
    }

    /*
     * ZEND_AST_IF is a list type that has one child: ZEND_AST_IF_ELEM. To
     * drill deeper into the IF body, we drill into the 2nd child of the
     * ZEND_AST_IF_ELEM.
     */
    if (ast->kind == ZEND_AST_IF) {
        list = zend_ast_get_list(ast);
        if (list->child[0]->lineno < to_insert->lineno) {
            for (i = list->children - 1; i >= 0; i--) {
                current = list->child[i];
                if (inject_ast(current, to_insert) == SUCCESS) {
                    return SUCCESS;
                }
            }
        }
    }

    if (ast->lineno > to_insert->lineno) {
        return FAILURE;
    }

    if (ast->kind == ZEND_AST_STMT_LIST) {
        list = zend_ast_get_list(ast);

        for (i = list->children - 1; i >= 0; i--) {
            current = list->child[i];

            /*
             * If the candidate line is before the snapshot point, we could be
             * on whitespace or if the candidate line is a list or special type,
             * we need to drill in further.
             */
            if (current->lineno < to_insert->lineno &&
                i < list->children - 1 &&
                current->kind >> ZEND_AST_IS_LIST_SHIFT != 1 &&
                current->kind >> ZEND_AST_SPECIAL_SHIFT != 1 &&
                current->kind >> ZEND_AST_NUM_CHILDREN_SHIFT != 4) {

                if (inject_ast(current, to_insert) != SUCCESS) {
                    current = list->child[i+1];
                    if (inject_ast(current, to_insert) != SUCCESS) {
                        list->child[i+1] = (zend_ast *)to_insert;
                        to_insert->child[1] = current;
                    }
                }
                return SUCCESS;
            }

            if (current->lineno <= to_insert->lineno) {
                /* if not yet injected, inject the debugger code */
                if (inject_ast(current, to_insert) != SUCCESS) {
                    to_insert->child[1] = current;
                    list->child[i] = (zend_ast *)to_insert;
                }
                return SUCCESS;
            }
        }

    } else if (ast->kind >> ZEND_AST_IS_LIST_SHIFT == 1) {
        list = zend_ast_get_list(ast);

        for (i = list->children - 1; i >= 0; i--) {
            current = list->child[i];
            if (inject_ast(current, to_insert) == SUCCESS) {
                return SUCCESS;
            }
        }

    } else if (ast->kind >> ZEND_AST_SPECIAL_SHIFT == 1) {
        switch(ast->kind) {
            case ZEND_AST_FUNC_DECL:
            case ZEND_AST_CLOSURE:
            case ZEND_AST_METHOD:
            case ZEND_AST_CLASS:
                decl = (zend_ast_decl *)ast;
                /* For decl, the 3rd child is the body of the declaration */
                return inject_ast(decl->child[2], to_insert);
        }
    } else {
        /* number of nodes */
        num_children = ast->kind >> ZEND_AST_NUM_CHILDREN_SHIFT;
        for (i = num_children - 1; i >= 0; i--) {
            current = ast->child[i];
            if (inject_ast(current, to_insert) == SUCCESS) {
                return SUCCESS;
            }
        }
    }

    return FAILURE;
}

/**
 * Helper to fill in initialized PHP array with the ids of the currently
 * injected breakpoint ids for a given file.
 */
static void fill_breakpoint_ids(zval *array, HashTable *breakpoints)
{
    int i;
    zend_string *breakpoint_id, *breakpoint_id2;
    ZEND_HASH_FOREACH_KEY(breakpoints, i, breakpoint_id) {
        breakpoint_id2 = zend_string_dup(breakpoint_id, 0);
        add_next_index_str(array, breakpoint_id2);
    } ZEND_HASH_FOREACH_END();
}

/**
 * Fills an initialized PHP array with all of the currently injected breakpoint
 * ids grouped by the file they are injected into.
 */
void stackdriver_list_breakpoint_ids(zval *return_value)
{
    HashTable *breakpoints;
    zend_string *filename;
    ZEND_HASH_FOREACH_STR_KEY_PTR(&registered_breakpoints, filename, breakpoints) {
        zval breakpoint_ids;
        array_init(&breakpoint_ids);
        fill_breakpoint_ids(&breakpoint_ids, breakpoints);
        add_assoc_zval_ex(return_value, ZSTR_VAL(filename), ZSTR_LEN(filename), &breakpoint_ids);
    } ZEND_HASH_FOREACH_END();
}

/**
 * Returns SUCCESS if the extension has previously injected the specified
 * breakpoint_id in the specified file.
 */
int stackdriver_debugger_breakpoint_injected(zend_string *filename, zend_string *breakpoint_id)
{
    HashTable *breakpoints = zend_hash_find_ptr(&registered_breakpoints, filename);
    zval *zv;
    if (breakpoints == NULL) {
        return FAILURE;
    }

    zv = zend_hash_find(breakpoints, breakpoint_id);
    if (zv == NULL) {
        return FAILURE;
    }
    return SUCCESS;
}

static void reset_registered_breakpoints_for_filename(zend_string *filename)
{
    zend_string *filename2;
    HashTable *breakpoints = zend_hash_find_ptr(&registered_breakpoints, filename);
    if (breakpoints != NULL) {
        zend_hash_clean(breakpoints);
    } else {
        /**
         * Persistent string dup as the filename is request based and we need
         * it to live between requests.
         */
        filename2 = zend_string_dup(filename, 1);

        /* Use malloc directly because we are not handling a request */
        breakpoints = malloc(sizeof(HashTable));
        zend_hash_init(breakpoints, 16, NULL, NULL, 1);
        zend_hash_add_ptr(&registered_breakpoints, filename2, breakpoints);
    }
}

static void register_breakpoint_id(zend_string *filename, zend_string *id)
{
    zend_string *id2 = zend_string_dup(id, 1);
    HashTable *breakpoints = zend_hash_find_ptr(&registered_breakpoints, filename);
    zend_hash_add_empty_element(breakpoints, id2);
}

/**
 * This function replaces the original `zend_ast_process` function. If one was
 * previously provided, call that one after this one.
 */
void stackdriver_debugger_ast_process(zend_ast *ast)
{
    HashTable *ht;
    stackdriver_debugger_snapshot_t *snapshot;
    stackdriver_debugger_logpoint_t *logpoint;
    zend_ast_list *to_insert;
    zend_string *filename = zend_get_compiled_filename();

    zval *snapshots = zend_hash_find(STACKDRIVER_DEBUGGER_G(snapshots_by_file), filename);
    zval *logpoints = zend_hash_find(STACKDRIVER_DEBUGGER_G(logpoints_by_file), filename);

    if (snapshots != NULL || logpoints != NULL) {
        reset_registered_breakpoints_for_filename(filename);
    }

    if (snapshots != NULL) {
        ht = Z_ARR_P(snapshots);

        ZEND_HASH_FOREACH_PTR(ht, snapshot) {
            to_insert = create_debugger_ast(
                "stackdriver_debugger_snapshot",
                snapshot->id,
                snapshot->lineno
            );
            if (inject_ast(ast, to_insert) == SUCCESS) {
                register_breakpoint_id(filename, snapshot->id);
            } else {
                // failed to insert
            }
        } ZEND_HASH_FOREACH_END();
    }

    if (logpoints != NULL) {
        ht = Z_ARR_P(logpoints);

        ZEND_HASH_FOREACH_PTR(ht, logpoint) {
            to_insert = create_debugger_ast(
                "stackdriver_debugger_logpoint",
                logpoint->id,
                logpoint->lineno
            );
            if (inject_ast(ast, to_insert) == SUCCESS) {
                register_breakpoint_id(filename, logpoint->id);
            } else {
                // failed to insert
            }
        } ZEND_HASH_FOREACH_END();
    }

    /* call the original zend_ast_process function if one was set */
    if (original_zend_ast_process) {
        original_zend_ast_process(ast);
    }
}

/**
 * Compile the AST for the provided source code.
 */
static int compile_ast(zend_string *source, zend_ast **ast_p, zend_lex_state *original_lex_state)
{
    zval source_zval;

    ZVAL_STR(&source_zval, source);
    Z_TRY_ADDREF(source_zval);
    zend_save_lexical_state(original_lex_state);

    if (zend_prepare_string_for_scanning(&source_zval, "") == FAILURE) {
        zend_restore_lexical_state(original_lex_state);
        return FAILURE;
    }

    CG(ast) = NULL;
    CG(ast_arena) = zend_arena_create(1024 * 32);

    zval_dtor(&source_zval);

    if (zendparse() != 0) {
        /* Error parsing the string */
        zend_ast_destroy(CG(ast));
        zend_arena_destroy(CG(ast_arena));
        CG(ast) = NULL;
        CG(ast_arena) = NULL;
        zend_restore_lexical_state(original_lex_state);

        if (EG(exception) != NULL) {
            zend_clear_exception();
            return FAILURE;
        }

        return FAILURE;
    }

    *ast_p = CG(ast);
    return SUCCESS;
}

/**
 * Determine if the allowed function call is whitelisted.
 */
static int valid_debugger_call(zend_string *function_name)
{
    if (zend_hash_find(&global_whitelisted_functions, function_name) != NULL) {
        return SUCCESS;
    }

    if (STACKDRIVER_DEBUGGER_G(user_whitelisted_functions) &&
        zend_hash_find(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions), function_name) != NULL) {
        return SUCCESS;
    }

    return FAILURE;
}

/**
 * Determine if the allowed method call is whitelisted.
 */
static int valid_debugger_method_call(zend_string *method_name)
{
    if (STACKDRIVER_DEBUGGER_G(user_whitelisted_methods) &&
        zend_hash_find(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods), method_name) != NULL) {
        return SUCCESS;
    }
    return FAILURE;
}

static int valid_debugger_call_ast(zend_ast *ast)
{
    zend_string *function_name = zend_ast_get_str(ast->child[0]);
    if (function_name) {
        return valid_debugger_call(function_name);
    }
    return FAILURE;
}

static int valid_debugger_method_call_ast(zend_ast *ast)
{
    zend_string *method_name = zend_ast_get_str(ast->child[1]);
    if (method_name) {
        return valid_debugger_method_call(method_name);
    }
    return FAILURE;
}

static int valid_debugger_static_call_ast(zend_ast *ast)
{
    zend_string *class_name = zend_ast_get_str(ast->child[0]);
    zend_string *function_name = zend_ast_get_str(ast->child[1]);
    int len = class_name->len + function_name->len + 2;
    zend_string *result = zend_string_alloc(len, 0);

    strcpy(ZSTR_VAL(result), class_name->val);
    strcat(ZSTR_VAL(result), "::");
    strcat(ZSTR_VAL(result), function_name->val);

    if (valid_debugger_call(result) == SUCCESS) {
        zend_string_release(result);
        return SUCCESS;
    }
    zend_string_release(result);
    return FAILURE;
}

/**
 * Determine whether a provided AST is valid for evaluation or as a
 * breakpoint condition.
 */
static int valid_debugger_ast(zend_ast *ast)
{
    int i, num_children;
    zend_ast *current;
    zend_ast_list *list;

    /* An empty node/statement will be null */
    if (ast == NULL) {
        return SUCCESS;
    }

    if (ast->kind >> ZEND_AST_IS_LIST_SHIFT == 1) {
        /* For list types, all children must be valid */
        list = (zend_ast_list*)ast;
        for (i = 0; i < list->children; i++) {
            current = list->child[i];
            if (valid_debugger_ast(list->child[i]) != SUCCESS) {
                return FAILURE;
            }
        }
        return SUCCESS;
    } else {
        switch (ast->kind) {
            case ZEND_AST_CALL:
                if (valid_debugger_call_ast(ast) == SUCCESS &&
                    valid_debugger_ast(ast->child[1]) == SUCCESS) {
                    return SUCCESS;
                }
                return FAILURE;
            case ZEND_AST_STATIC_CALL:
                if (valid_debugger_static_call_ast(ast) == SUCCESS &&
                    valid_debugger_ast(ast->child[2]) == SUCCESS) {
                    return SUCCESS;
                }
                return FAILURE;
            case ZEND_AST_METHOD_CALL:
                if (valid_debugger_ast(ast->child[0]) == SUCCESS) {
                    if (valid_debugger_method_call_ast(ast) == SUCCESS &&
                        valid_debugger_ast(ast->child[2]) == SUCCESS) {
                        return SUCCESS;
                    }
                }
                return FAILURE;
            /* special */
            case ZEND_AST_ZVAL:
            case ZEND_AST_ZNODE:
                return SUCCESS;

            /* 1 child */
            case ZEND_AST_VAR:
            case ZEND_AST_CONST:
            case ZEND_AST_UNARY_PLUS:
            case ZEND_AST_UNARY_MINUS:
            case ZEND_AST_CAST:
            case ZEND_AST_EMPTY:
            case ZEND_AST_ISSET:
            case ZEND_AST_UNARY_OP:
            case ZEND_AST_DIM: /* What is this? */
            case ZEND_AST_PROP:
            case ZEND_AST_STATIC_PROP:
            case ZEND_AST_CLASS_CONST:
                if (valid_debugger_ast(ast->child[0]) == SUCCESS) {
                    return SUCCESS;
                }
                return FAILURE;

            /* 2 children */
            case ZEND_AST_BINARY_OP:
            case ZEND_AST_GREATER:
            case ZEND_AST_GREATER_EQUAL:
            case ZEND_AST_AND:
            case ZEND_AST_OR:
            case ZEND_AST_ARRAY_ELEM:
            case ZEND_AST_INSTANCEOF:
                if (valid_debugger_ast(ast->child[0]) == SUCCESS &&
                    valid_debugger_ast(ast->child[1]) == SUCCESS) {
                    return SUCCESS;
                }
                return FAILURE;

            /* 3 children */
            case ZEND_AST_CONDITIONAL:
            case ZEND_AST_PROP_ELEM:
            case ZEND_AST_CONST_ELEM:
                if (valid_debugger_ast(ast->child[0]) == SUCCESS &&
                    valid_debugger_ast(ast->child[1]) == SUCCESS &&
                    valid_debugger_ast(ast->child[2]) == SUCCESS) {
                    return SUCCESS;
                }
                return FAILURE;
        }

        return FAILURE;
    }
    return FAILURE;
}

/**
 * Validate that the provided statement is valid for debugging. This ensures
 * that the statement will compile and contains only valid operations.
 */
int valid_debugger_statement(zend_string *statement)
{
    zend_lex_state original_lex_state;
    zend_ast *ast_p, *old_ast = CG(ast);
    zend_arena *old_arena = CG(ast_arena);

    /*
     * Append ';' to the end for lexing/parsing. Evaluating the statement
     * doesn't require a ';' at the end of the statement and could actually
     * change the semantics of the return value;
     */
    zend_string *extended_statement = strpprintf(ZSTR_LEN(statement) + 1, "%s%c", ZSTR_VAL(statement), ';');
    if (compile_ast(extended_statement, &ast_p, &original_lex_state) != SUCCESS) {
        zend_string_release(extended_statement);
        php_error_docref(NULL, E_WARNING, "Unable to compile condition.");
        return FAILURE;
    }
    zend_string_release(extended_statement);

    if (valid_debugger_ast(ast_p) != SUCCESS) {
        php_error_docref(NULL, E_WARNING, "Condition contains invalid operations");
        zend_ast_destroy(CG(ast));
        zend_arena_destroy(CG(ast_arena));
        zend_restore_lexical_state(&original_lex_state);
        CG(ast) = NULL;
        CG(ast_arena) = NULL;
        return FAILURE;
    }

    zend_ast_destroy(CG(ast));
    zend_arena_destroy(CG(ast_arena));
    zend_restore_lexical_state(&original_lex_state);
    CG(ast) = old_ast;
    CG(ast_arena) = old_arena;

    return SUCCESS;
}

static void register_user_whitelisted_str(const char *str, int len, HashTable * hashTable)
{
    char *key = NULL, *last = NULL;
    char *tmp = estrndup(str, len);

    for (key = php_strtok_r(tmp, ",", &last); key; key = php_strtok_r(NULL, ",", &last)) {
        zend_hash_str_add_empty_element(hashTable, key, strlen(key));
    }
    efree(tmp);
}

static void register_user_whitelisted_functions(zend_string *ini_setting)
{
    register_user_whitelisted_str(
        ZSTR_VAL(ini_setting),
        ZSTR_LEN(ini_setting),
        STACKDRIVER_DEBUGGER_G(user_whitelisted_functions)
    );
}

static void register_user_whitelisted_methods(zend_string *ini_setting)
{
    register_user_whitelisted_str(
        ZSTR_VAL(ini_setting),
        ZSTR_LEN(ini_setting),
        STACKDRIVER_DEBUGGER_G(user_whitelisted_methods)
    );
}

#define WHITELIST_FUNCTION(function_name) zend_hash_str_add_empty_element(ht, function_name, strlen(function_name))

/*
 * Registers a hard-coded list of functions to allow in conditions and
 * expressions.
 */
static int register_whitelisted_functions(HashTable *ht)
{
    /* Array functions */
    WHITELIST_FUNCTION("array_change_key_case");
    WHITELIST_FUNCTION("array_chunk");
    WHITELIST_FUNCTION("array_column");
    WHITELIST_FUNCTION("array_combine");
    WHITELIST_FUNCTION("array_count_values");
    WHITELIST_FUNCTION("array_diff_assoc");
    WHITELIST_FUNCTION("array_diff_key");
    WHITELIST_FUNCTION("array_diff_uassoc");
    WHITELIST_FUNCTION("array_diff_ukey");
    WHITELIST_FUNCTION("array_diff");
    WHITELIST_FUNCTION("array_fill_keys");
    WHITELIST_FUNCTION("array_fill");
    WHITELIST_FUNCTION("array_filter");
    WHITELIST_FUNCTION("array_flip");
    WHITELIST_FUNCTION("array_intersect_assoc");
    WHITELIST_FUNCTION("array_intersect_key");
    WHITELIST_FUNCTION("array_intersect_uassoc");
    WHITELIST_FUNCTION("array_intersect_ukey");
    WHITELIST_FUNCTION("array_key_exists");
    WHITELIST_FUNCTION("array_keys");
    WHITELIST_FUNCTION("array_map");
    WHITELIST_FUNCTION("array_merge_recursive");
    WHITELIST_FUNCTION("array_merge");
    WHITELIST_FUNCTION("array_multisort");
    WHITELIST_FUNCTION("array_pad");
    WHITELIST_FUNCTION("array_product");
    WHITELIST_FUNCTION("array_rand");
    WHITELIST_FUNCTION("array_reduce");
    WHITELIST_FUNCTION("array_replace_recursive");
    WHITELIST_FUNCTION("array_replace");
    WHITELIST_FUNCTION("array_reverse");
    WHITELIST_FUNCTION("array_search");
    WHITELIST_FUNCTION("array_slice");
    WHITELIST_FUNCTION("array_splice");
    WHITELIST_FUNCTION("array_sum");
    WHITELIST_FUNCTION("array_udiff_assoc");
    WHITELIST_FUNCTION("array_udiff_uassoc");
    WHITELIST_FUNCTION("array_udiff");
    WHITELIST_FUNCTION("array_uintersect_assoc");
    WHITELIST_FUNCTION("array_uintersect_uassoc");
    WHITELIST_FUNCTION("array_uintersect");
    WHITELIST_FUNCTION("array_unique");
    WHITELIST_FUNCTION("array_values");
    WHITELIST_FUNCTION("array_walk_recursive");
    WHITELIST_FUNCTION("array_walk");
    WHITELIST_FUNCTION("compact");
    WHITELIST_FUNCTION("count");
    WHITELIST_FUNCTION("current");
    WHITELIST_FUNCTION("in_array");
    WHITELIST_FUNCTION("key_exists");
    WHITELIST_FUNCTION("key");
    WHITELIST_FUNCTION("pos");
    WHITELIST_FUNCTION("range");
    WHITELIST_FUNCTION("sizeof");

    /* Class functions */
    WHITELIST_FUNCTION("class_exists");
    WHITELIST_FUNCTION("get_called_class");
    WHITELIST_FUNCTION("get_class_methods");
    WHITELIST_FUNCTION("get_class_vars");
    WHITELIST_FUNCTION("get_class");
    WHITELIST_FUNCTION("get_declared_classes");
    WHITELIST_FUNCTION("get_declared_interfaces");
    WHITELIST_FUNCTION("get_declared_traits");
    WHITELIST_FUNCTION("get_object_vars");
    WHITELIST_FUNCTION("get_parent_class");
    WHITELIST_FUNCTION("interface_exists");
    WHITELIST_FUNCTION("is_a");
    WHITELIST_FUNCTION("is_subclass_of");
    WHITELIST_FUNCTION("method_exists");
    WHITELIST_FUNCTION("property_exists");
    WHITELIST_FUNCTION("trait_exists");

    /* Configuration handling: http://php.net/manual/en/ref.info.php */
    WHITELIST_FUNCTION("extension_loaded");
    WHITELIST_FUNCTION("gc_enabled");
    WHITELIST_FUNCTION("get_cfg_var");
    WHITELIST_FUNCTION("get_current_user");
    WHITELIST_FUNCTION("get_defined_constants");
    WHITELIST_FUNCTION("get_extension_funcs");
    WHITELIST_FUNCTION("get_include_path");
    WHITELIST_FUNCTION("get_included_files");
    WHITELIST_FUNCTION("get_loaded_extensions");
    WHITELIST_FUNCTION("get_magic_quotes_gpc");
    WHITELIST_FUNCTION("get_magic_quotes_runtime");
    WHITELIST_FUNCTION("get_required_files");
    WHITELIST_FUNCTION("get_resoruces");
    WHITELIST_FUNCTION("getenv");
    WHITELIST_FUNCTION("getlastmod");
    WHITELIST_FUNCTION("getmygid");
    WHITELIST_FUNCTION("getmyinode");
    WHITELIST_FUNCTION("getmypid");
    WHITELIST_FUNCTION("getmyuid");
    WHITELIST_FUNCTION("getrusage");
    WHITELIST_FUNCTION("ini_get_all");
    WHITELIST_FUNCTION("ini_get");
    WHITELIST_FUNCTION("memory_get_peak_usage");
    WHITELIST_FUNCTION("memory_get_usage");
    WHITELIST_FUNCTION("php_ini_loaded_file");
    WHITELIST_FUNCTION("php_ini_scanned_files");
    WHITELIST_FUNCTION("php_logo_guid");
    WHITELIST_FUNCTION("php_sapi_name");
    WHITELIST_FUNCTION("php_uname");
    WHITELIST_FUNCTION("phpversion");
    WHITELIST_FUNCTION("sys_get_temp_dir");
    WHITELIST_FUNCTION("version_compare");
    WHITELIST_FUNCTION("zend_logo_guid");
    WHITELIST_FUNCTION("zend_thread_id");
    WHITELIST_FUNCTION("zend_version");

    /* Function handling: http://php.net/manual/en/book.funchand.php */
    WHITELIST_FUNCTION("func_get_arg");
    WHITELIST_FUNCTION("func_get_args");
    WHITELIST_FUNCTION("func_num_args");
    WHITELIST_FUNCTION("function_exists");
    WHITELIST_FUNCTION("get_defined_function");

    /* String handling: */
    WHITELIST_FUNCTION("addcslashes");
    WHITELIST_FUNCTION("addslashes");
    WHITELIST_FUNCTION("bin2hex");
    WHITELIST_FUNCTION("chop");
    WHITELIST_FUNCTION("chr");
    WHITELIST_FUNCTION("chunk_split");
    WHITELIST_FUNCTION("convert_cyr_string");
    WHITELIST_FUNCTION("convert_uudecode");
    WHITELIST_FUNCTION("convert_uuencode");
    WHITELIST_FUNCTION("count_chars");
    WHITELIST_FUNCTION("crc32");
    WHITELIST_FUNCTION("crypt");
    WHITELIST_FUNCTION("explode");
    WHITELIST_FUNCTION("get_html_translation_table");
    WHITELIST_FUNCTION("hebrev");
    WHITELIST_FUNCTION("hebrevc");
    WHITELIST_FUNCTION("hex2bin");
    WHITELIST_FUNCTION("html_entity_decode");
    WHITELIST_FUNCTION("htmlentities");
    WHITELIST_FUNCTION("htmlspecialchars_decode");
    WHITELIST_FUNCTION("html_specialchars");
    WHITELIST_FUNCTION("implode");
    WHITELIST_FUNCTION("join");
    WHITELIST_FUNCTION("lcfirst");
    WHITELIST_FUNCTION("levenshtein");
    WHITELIST_FUNCTION("localeconv");
    WHITELIST_FUNCTION("ltrim");
    WHITELIST_FUNCTION("md5file");
    WHITELIST_FUNCTION("md5");
    WHITELIST_FUNCTION("metaphone");
    WHITELIST_FUNCTION("money_format");
    WHITELIST_FUNCTION("nl_langinfo");
    WHITELIST_FUNCTION("nl2br");
    WHITELIST_FUNCTION("number_format");
    WHITELIST_FUNCTION("ord");
    WHITELIST_FUNCTION("quoted_printable_decode");
    WHITELIST_FUNCTION("quoted_printable_encode");
    WHITELIST_FUNCTION("quotemeta");
    WHITELIST_FUNCTION("rtrim");
    WHITELIST_FUNCTION("sha1_file");
    WHITELIST_FUNCTION("sha1");
    WHITELIST_FUNCTION("soundex");
    WHITELIST_FUNCTION("sprintf");
    WHITELIST_FUNCTION("str_getcsv");
    WHITELIST_FUNCTION("str_pad");
    WHITELIST_FUNCTION("str_repeat");
    WHITELIST_FUNCTION("str_rot13");
    WHITELIST_FUNCTION("str_shuffle");
    WHITELIST_FUNCTION("str_split");
    WHITELIST_FUNCTION("str_word_count");
    WHITELIST_FUNCTION("strcasecmp");
    WHITELIST_FUNCTION("strchr");
    WHITELIST_FUNCTION("strcmp");
    WHITELIST_FUNCTION("strcoll");
    WHITELIST_FUNCTION("strcspn");
    WHITELIST_FUNCTION("strip_tags");
    WHITELIST_FUNCTION("stripcslashes");
    WHITELIST_FUNCTION("stripos");
    WHITELIST_FUNCTION("stripslashes");
    WHITELIST_FUNCTION("stristr");
    WHITELIST_FUNCTION("strlen");
    WHITELIST_FUNCTION("strnatcasecmp");
    WHITELIST_FUNCTION("strnatcmp");
    WHITELIST_FUNCTION("strncasecmp");
    WHITELIST_FUNCTION("strncmp");
    WHITELIST_FUNCTION("strpbrk");
    WHITELIST_FUNCTION("strpos");
    WHITELIST_FUNCTION("strrchr");
    WHITELIST_FUNCTION("strrev");
    WHITELIST_FUNCTION("strripos");
    WHITELIST_FUNCTION("strrpos");
    WHITELIST_FUNCTION("strspn");
    WHITELIST_FUNCTION("strstr");
    WHITELIST_FUNCTION("strtok");
    WHITELIST_FUNCTION("strtolower");
    WHITELIST_FUNCTION("strtoupper");
    WHITELIST_FUNCTION("strtr");
    WHITELIST_FUNCTION("substr_compare");
    WHITELIST_FUNCTION("substr_count");
    WHITELIST_FUNCTION("substr_replace");
    WHITELIST_FUNCTION("substr");
    WHITELIST_FUNCTION("trim");
    WHITELIST_FUNCTION("ucfirst");
    WHITELIST_FUNCTION("ucwords");
    WHITELIST_FUNCTION("wordwrap");

    /* Variable handling: http://php.net/manual/en/book.var.php */
    WHITELIST_FUNCTION("boolval");
    WHITELIST_FUNCTION("doubleval");
    WHITELIST_FUNCTION("empty");
    WHITELIST_FUNCTION("float_val");
    WHITELIST_FUNCTION("get_defined_vars");
    WHITELIST_FUNCTION("get_resource_type");
    WHITELIST_FUNCTION("gettype");
    WHITELIST_FUNCTION("intval");
    WHITELIST_FUNCTION("is_array");
    WHITELIST_FUNCTION("is_bool");
    WHITELIST_FUNCTION("is_callable");
    WHITELIST_FUNCTION("is_double");
    WHITELIST_FUNCTION("is_float");
    WHITELIST_FUNCTION("is_int");
    WHITELIST_FUNCTION("is_integer");
    WHITELIST_FUNCTION("is_iterable");
    WHITELIST_FUNCTION("is_long");
    WHITELIST_FUNCTION("is_null");
    WHITELIST_FUNCTION("is_numeric");
    WHITELIST_FUNCTION("is_object");
    WHITELIST_FUNCTION("is_real");
    WHITELIST_FUNCTION("is_resource");
    WHITELIST_FUNCTION("is_scalar");
    WHITELIST_FUNCTION("is_string");
    WHITELIST_FUNCTION("isset");
    WHITELIST_FUNCTION("serialize");
    WHITELIST_FUNCTION("settype");
    WHITELIST_FUNCTION("strval");
    WHITELIST_FUNCTION("unserialize");

    return SUCCESS;
}

static void ast_to_clean_dtor(zval *zv)
{
    zend_ast *ast = (zend_ast *)Z_PTR_P(zv);
    efree(ast);
}

/**
 * Request initialization lifecycle hook. Sets up the function whitelist.
 */
int stackdriver_debugger_ast_rinit(TSRMLS_D)
{
    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions), 8, NULL, ZVAL_PTR_DTOR, 1);

    char *ini = INI_STR(PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_FUNCTIONS);
    if (ini) {
        register_user_whitelisted_str(
            ini,
            strlen(ini),
            STACKDRIVER_DEBUGGER_G(user_whitelisted_functions)
        );
    }

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods), 8, NULL, ZVAL_PTR_DTOR, 1);

    ini = INI_STR(PHP_STACKDRIVER_DEBUGGER_INI_WHITELISTED_METHODS);
    if (ini) {
        register_user_whitelisted_str(
                ini,
                strlen(ini),
                STACKDRIVER_DEBUGGER_G(user_whitelisted_methods)
        );
    }

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(ast_to_clean));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(ast_to_clean), 8, NULL, ast_to_clean_dtor, 1);

    return SUCCESS;
}

/**
 * Request shutdown lifecycle hook. Cleans up the function whitelist.
 */
int stackdriver_debugger_ast_rshutdown(TSRMLS_D)
{
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods));
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(ast_to_clean));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(ast_to_clean));

    return SUCCESS;
}

/**
 * Callback for destroying each value stored in registered_breakpoints global.
 */
static void breakpoints_dtor(zval *zv)
{
    /**
     * This HashTable is allocated with malloc in
     * reset_registered_breakpoints_for_filename.
     */
    HashTable *ht = Z_PTR_P(zv);
    zend_hash_destroy(ht);

    /* use free directly because we are not handling a request */
    free(ht);
    ZVAL_PTR_DTOR(zv);
}

/**
 * Module initialization lifecycle hook. Registers our AST processor so we can
 * modify the AST after compilation.
 */
int stackdriver_debugger_ast_minit(INIT_FUNC_ARGS)
{
    /*
     * Save original zend_ast_process function and use our own to modify the
     * AST.
     */
    original_zend_ast_process = zend_ast_process;
    zend_ast_process = stackdriver_debugger_ast_process;

    /* Setup storage for whitelisted functions */
    zend_hash_init(&global_whitelisted_functions, 1024, NULL, ZVAL_PTR_DTOR, 1);
    register_whitelisted_functions(&global_whitelisted_functions);

    /* Setup storage for breakpoints by filename */
    zend_hash_init(&registered_breakpoints, 64, NULL, breakpoints_dtor, 1);

    return SUCCESS;
}

/**
 * Module shutdown lifecycle hook. Deregisters our AST processor.
 */
int stackdriver_debugger_ast_mshutdown(SHUTDOWN_FUNC_ARGS)
{
    zend_ast_process = original_zend_ast_process;
    zend_hash_destroy(&global_whitelisted_functions);
    zend_hash_destroy(&registered_breakpoints);

    return SUCCESS;
}

/**
 * Callback for when the user changes the function whitelist php.ini setting.
 */
PHP_INI_MH(OnUpdate_stackdriver_debugger_whitelisted_functions)
{
    /* Only use this mechanism for ini_set (runtime stage) */
    if (new_value != NULL && stage & ZEND_INI_STAGE_RUNTIME) {
        zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions));
        zend_hash_init(STACKDRIVER_DEBUGGER_G(user_whitelisted_functions), 8, NULL, ZVAL_PTR_DTOR, 1);
        register_user_whitelisted_str(
            ZSTR_VAL(new_value),
            ZSTR_LEN(new_value),
            STACKDRIVER_DEBUGGER_G(user_whitelisted_functions)
        );
    }
    return SUCCESS;
}

/**
 * Callback for when the user changes the method function whitelist php.ini setting.
 */
PHP_INI_MH(OnUpdate_stackdriver_debugger_whitelisted_methods)
{
    /* Only use this mechanism for ini_set (runtime stage) */
    if (new_value != NULL && stage & ZEND_INI_STAGE_RUNTIME) {
        zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods));
        zend_hash_init(STACKDRIVER_DEBUGGER_G(user_whitelisted_methods), 8, NULL, ZVAL_PTR_DTOR, 1);
        register_user_whitelisted_str(
                ZSTR_VAL(new_value),
                ZSTR_LEN(new_value),
                STACKDRIVER_DEBUGGER_G(user_whitelisted_methods)
        );
    }
    return SUCCESS;
}
