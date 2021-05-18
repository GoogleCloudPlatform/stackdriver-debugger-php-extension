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
static HashTable global_allowed_functions;

/* map of filename -> (map of breakpoint id -> nil) */
static HashTable registered_breakpoints;

static inline size_t ast_list_size(uint32_t children) {
	return sizeof(zend_ast_list) - sizeof(zend_ast *) + sizeof(zend_ast *) * children;
}

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
    zend_ast_zval *var = NULL, *snapshot_id = NULL;
    zend_ast_list *new_list, *arg_list;

    var = emalloc(sizeof(zend_ast_zval));
    var->kind = ZEND_AST_ZVAL;
    ZVAL_STRING(&var->val, callback);
    Z_LINENO(var->val) = lineno;
    zend_hash_next_index_insert_ptr(STACKDRIVER_DEBUGGER_G(ast_to_clean), var);

    snapshot_id = emalloc(sizeof(zend_ast_zval));
    snapshot_id->kind = ZEND_AST_ZVAL;
    ZVAL_STR(&snapshot_id->val, breakpoint_id);
    Z_LINENO(snapshot_id->val) = lineno;
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

    if (ast == NULL || to_insert == NULL) {
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
        breakpoint_id2 = zend_string_init(ZSTR_VAL(breakpoint_id), ZSTR_LEN(breakpoint_id), 0);
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
        filename2 = zend_string_init(ZSTR_VAL(filename), ZSTR_LEN(filename), 1);

        /* Use malloc directly because we are not handling a request */
        breakpoints = malloc(sizeof(HashTable));
        zend_hash_init(breakpoints, 16, NULL, NULL, 1);
        zend_hash_add_ptr(&registered_breakpoints, filename2, breakpoints);
    }
}

static void register_breakpoint_id(zend_string *filename, zend_string *id)
{
    zend_string *id2 = zend_string_init(ZSTR_VAL(id), ZSTR_LEN(id), 1);
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
    zend_ast_list *to_insert = NULL;
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
            if (logpoint != NULL && logpoint->id != NULL) {
                to_insert = create_debugger_ast(
                    "stackdriver_debugger_logpoint",
                    logpoint->id,
                    logpoint->lineno
                );
                if (ast != NULL && to_insert != NULL && inject_ast(ast, to_insert) == SUCCESS) {
                    register_breakpoint_id(filename, logpoint->id);
                } else {
                    // failed to insert
                }
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

    ZVAL_STR_COPY(&source_zval, source);
    zend_save_lexical_state(original_lex_state);

    zend_prepare_string_for_scanning(&source_zval, "");
    // if (zend_prepare_string_for_scanning(&source_zval, "") == FAILURE) {
    //     zend_restore_lexical_state(original_lex_state);
    //     return FAILURE;
    // }

    CG(ast) = NULL;
    CG(ast_arena) = zend_arena_create(1024 * 32);

    #if PHP_VERSION_ID < 70400
    zval_dtor(&source_zval);
    #else
    zval_ptr_dtor_str(&source_zval);
    #endif

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
 * Determine if the function call is allowed.
 */
static int valid_debugger_call(zend_string *function_name)
{
    if (zend_hash_find(&global_allowed_functions, function_name) != NULL) {
        return SUCCESS;
    }

    if (STACKDRIVER_DEBUGGER_G(user_allowed_functions) &&
        zend_hash_find(STACKDRIVER_DEBUGGER_G(user_allowed_functions), function_name) != NULL) {
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
                if (valid_debugger_method_call_ast(ast) == SUCCESS &&
                    valid_debugger_ast(ast->child[2]) == SUCCESS) {
                    return SUCCESS;
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

static void register_user_allowed_functions_str(const char *str, int len)
{
    char *key = NULL, *last = NULL;
    char *tmp = estrndup(str, len);

    for (key = php_strtok_r(tmp, ",", &last); key; key = php_strtok_r(NULL, ",", &last)) {
        zend_hash_str_add_empty_element(STACKDRIVER_DEBUGGER_G(user_allowed_functions), key, strlen(key));
    }
    efree(tmp);
}

static void register_user_allowed_functions(zend_string *ini_setting)
{
    register_user_allowed_functions_str(ZSTR_VAL(ini_setting), ZSTR_LEN(ini_setting));
}

#define ALLOW_FUNCTION(function_name) zend_hash_str_add_empty_element(ht, function_name, strlen(function_name))

/*
 * Registers a hard-coded list of functions to allow in conditions and
 * expressions.
 */
static int register_allowed_functions(HashTable *ht)
{
    /* Array functions */
    ALLOW_FUNCTION("array_change_key_case");
    ALLOW_FUNCTION("array_chunk");
    ALLOW_FUNCTION("array_column");
    ALLOW_FUNCTION("array_combine");
    ALLOW_FUNCTION("array_count_values");
    ALLOW_FUNCTION("array_diff_assoc");
    ALLOW_FUNCTION("array_diff_key");
    ALLOW_FUNCTION("array_diff_uassoc");
    ALLOW_FUNCTION("array_diff_ukey");
    ALLOW_FUNCTION("array_diff");
    ALLOW_FUNCTION("array_fill_keys");
    ALLOW_FUNCTION("array_fill");
    ALLOW_FUNCTION("array_filter");
    ALLOW_FUNCTION("array_flip");
    ALLOW_FUNCTION("array_intersect_assoc");
    ALLOW_FUNCTION("array_intersect_key");
    ALLOW_FUNCTION("array_intersect_uassoc");
    ALLOW_FUNCTION("array_intersect_ukey");
    ALLOW_FUNCTION("array_key_exists");
    ALLOW_FUNCTION("array_keys");
    ALLOW_FUNCTION("array_map");
    ALLOW_FUNCTION("array_merge_recursive");
    ALLOW_FUNCTION("array_merge");
    ALLOW_FUNCTION("array_multisort");
    ALLOW_FUNCTION("array_pad");
    ALLOW_FUNCTION("array_product");
    ALLOW_FUNCTION("array_rand");
    ALLOW_FUNCTION("array_reduce");
    ALLOW_FUNCTION("array_replace_recursive");
    ALLOW_FUNCTION("array_replace");
    ALLOW_FUNCTION("array_reverse");
    ALLOW_FUNCTION("array_search");
    ALLOW_FUNCTION("array_slice");
    ALLOW_FUNCTION("array_splice");
    ALLOW_FUNCTION("array_sum");
    ALLOW_FUNCTION("array_udiff_assoc");
    ALLOW_FUNCTION("array_udiff_uassoc");
    ALLOW_FUNCTION("array_udiff");
    ALLOW_FUNCTION("array_uintersect_assoc");
    ALLOW_FUNCTION("array_uintersect_uassoc");
    ALLOW_FUNCTION("array_uintersect");
    ALLOW_FUNCTION("array_unique");
    ALLOW_FUNCTION("array_values");
    ALLOW_FUNCTION("array_walk_recursive");
    ALLOW_FUNCTION("array_walk");
    ALLOW_FUNCTION("compact");
    ALLOW_FUNCTION("count");
    ALLOW_FUNCTION("current");
    ALLOW_FUNCTION("in_array");
    ALLOW_FUNCTION("key_exists");
    ALLOW_FUNCTION("key");
    ALLOW_FUNCTION("pos");
    ALLOW_FUNCTION("range");
    ALLOW_FUNCTION("sizeof");

    /* Class functions */
    ALLOW_FUNCTION("class_exists");
    ALLOW_FUNCTION("get_called_class");
    ALLOW_FUNCTION("get_class_methods");
    ALLOW_FUNCTION("get_class_vars");
    ALLOW_FUNCTION("get_class");
    ALLOW_FUNCTION("get_declared_classes");
    ALLOW_FUNCTION("get_declared_interfaces");
    ALLOW_FUNCTION("get_declared_traits");
    ALLOW_FUNCTION("get_object_vars");
    ALLOW_FUNCTION("get_parent_class");
    ALLOW_FUNCTION("interface_exists");
    ALLOW_FUNCTION("is_a");
    ALLOW_FUNCTION("is_subclass_of");
    ALLOW_FUNCTION("method_exists");
    ALLOW_FUNCTION("property_exists");
    ALLOW_FUNCTION("trait_exists");

    /* Configuration handling: http://php.net/manual/en/ref.info.php */
    ALLOW_FUNCTION("extension_loaded");
    ALLOW_FUNCTION("gc_enabled");
    ALLOW_FUNCTION("get_cfg_var");
    ALLOW_FUNCTION("get_current_user");
    ALLOW_FUNCTION("get_defined_constants");
    ALLOW_FUNCTION("get_extension_funcs");
    ALLOW_FUNCTION("get_include_path");
    ALLOW_FUNCTION("get_included_files");
    ALLOW_FUNCTION("get_loaded_extensions");
    ALLOW_FUNCTION("get_magic_quotes_gpc");
    ALLOW_FUNCTION("get_magic_quotes_runtime");
    ALLOW_FUNCTION("get_required_files");
    ALLOW_FUNCTION("get_resoruces");
    ALLOW_FUNCTION("getenv");
    ALLOW_FUNCTION("getlastmod");
    ALLOW_FUNCTION("getmygid");
    ALLOW_FUNCTION("getmyinode");
    ALLOW_FUNCTION("getmypid");
    ALLOW_FUNCTION("getmyuid");
    ALLOW_FUNCTION("getrusage");
    ALLOW_FUNCTION("ini_get_all");
    ALLOW_FUNCTION("ini_get");
    ALLOW_FUNCTION("memory_get_peak_usage");
    ALLOW_FUNCTION("memory_get_usage");
    ALLOW_FUNCTION("php_ini_loaded_file");
    ALLOW_FUNCTION("php_ini_scanned_files");
    ALLOW_FUNCTION("php_logo_guid");
    ALLOW_FUNCTION("php_sapi_name");
    ALLOW_FUNCTION("php_uname");
    ALLOW_FUNCTION("phpversion");
    ALLOW_FUNCTION("sys_get_temp_dir");
    ALLOW_FUNCTION("version_compare");
    ALLOW_FUNCTION("zend_logo_guid");
    ALLOW_FUNCTION("zend_thread_id");
    ALLOW_FUNCTION("zend_version");

    /* Function handling: http://php.net/manual/en/book.funchand.php */
    ALLOW_FUNCTION("func_get_arg");
    ALLOW_FUNCTION("func_get_args");
    ALLOW_FUNCTION("func_num_args");
    ALLOW_FUNCTION("function_exists");
    ALLOW_FUNCTION("get_defined_function");

    /* String handling: */
    ALLOW_FUNCTION("addcslashes");
    ALLOW_FUNCTION("addslashes");
    ALLOW_FUNCTION("bin2hex");
    ALLOW_FUNCTION("chop");
    ALLOW_FUNCTION("chr");
    ALLOW_FUNCTION("chunk_split");
    ALLOW_FUNCTION("convert_cyr_string");
    ALLOW_FUNCTION("convert_uudecode");
    ALLOW_FUNCTION("convert_uuencode");
    ALLOW_FUNCTION("count_chars");
    ALLOW_FUNCTION("crc32");
    ALLOW_FUNCTION("crypt");
    ALLOW_FUNCTION("explode");
    ALLOW_FUNCTION("get_html_translation_table");
    ALLOW_FUNCTION("hebrev");
    ALLOW_FUNCTION("hebrevc");
    ALLOW_FUNCTION("hex2bin");
    ALLOW_FUNCTION("html_entity_decode");
    ALLOW_FUNCTION("htmlentities");
    ALLOW_FUNCTION("htmlspecialchars_decode");
    ALLOW_FUNCTION("html_specialchars");
    ALLOW_FUNCTION("implode");
    ALLOW_FUNCTION("join");
    ALLOW_FUNCTION("lcfirst");
    ALLOW_FUNCTION("levenshtein");
    ALLOW_FUNCTION("localeconv");
    ALLOW_FUNCTION("ltrim");
    ALLOW_FUNCTION("md5file");
    ALLOW_FUNCTION("md5");
    ALLOW_FUNCTION("metaphone");
    ALLOW_FUNCTION("money_format");
    ALLOW_FUNCTION("nl_langinfo");
    ALLOW_FUNCTION("nl2br");
    ALLOW_FUNCTION("number_format");
    ALLOW_FUNCTION("ord");
    ALLOW_FUNCTION("quoted_printable_decode");
    ALLOW_FUNCTION("quoted_printable_encode");
    ALLOW_FUNCTION("quotemeta");
    ALLOW_FUNCTION("rtrim");
    ALLOW_FUNCTION("sha1_file");
    ALLOW_FUNCTION("sha1");
    ALLOW_FUNCTION("soundex");
    ALLOW_FUNCTION("sprintf");
    ALLOW_FUNCTION("str_getcsv");
    ALLOW_FUNCTION("str_pad");
    ALLOW_FUNCTION("str_repeat");
    ALLOW_FUNCTION("str_rot13");
    ALLOW_FUNCTION("str_shuffle");
    ALLOW_FUNCTION("str_split");
    ALLOW_FUNCTION("str_word_count");
    ALLOW_FUNCTION("strcasecmp");
    ALLOW_FUNCTION("strchr");
    ALLOW_FUNCTION("strcmp");
    ALLOW_FUNCTION("strcoll");
    ALLOW_FUNCTION("strcspn");
    ALLOW_FUNCTION("strip_tags");
    ALLOW_FUNCTION("stripcslashes");
    ALLOW_FUNCTION("stripos");
    ALLOW_FUNCTION("stripslashes");
    ALLOW_FUNCTION("stristr");
    ALLOW_FUNCTION("strlen");
    ALLOW_FUNCTION("strnatcasecmp");
    ALLOW_FUNCTION("strnatcmp");
    ALLOW_FUNCTION("strncasecmp");
    ALLOW_FUNCTION("strncmp");
    ALLOW_FUNCTION("strpbrk");
    ALLOW_FUNCTION("strpos");
    ALLOW_FUNCTION("strrchr");
    ALLOW_FUNCTION("strrev");
    ALLOW_FUNCTION("strripos");
    ALLOW_FUNCTION("strrpos");
    ALLOW_FUNCTION("strspn");
    ALLOW_FUNCTION("strstr");
    ALLOW_FUNCTION("strtok");
    ALLOW_FUNCTION("strtolower");
    ALLOW_FUNCTION("strtoupper");
    ALLOW_FUNCTION("strtr");
    ALLOW_FUNCTION("substr_compare");
    ALLOW_FUNCTION("substr_count");
    ALLOW_FUNCTION("substr_replace");
    ALLOW_FUNCTION("substr");
    ALLOW_FUNCTION("trim");
    ALLOW_FUNCTION("ucfirst");
    ALLOW_FUNCTION("ucwords");
    ALLOW_FUNCTION("wordwrap");

    /* Variable handling: http://php.net/manual/en/book.var.php */
    ALLOW_FUNCTION("boolval");
    ALLOW_FUNCTION("doubleval");
    ALLOW_FUNCTION("empty");
    ALLOW_FUNCTION("float_val");
    ALLOW_FUNCTION("get_defined_vars");
    ALLOW_FUNCTION("get_resource_type");
    ALLOW_FUNCTION("gettype");
    ALLOW_FUNCTION("intval");
    ALLOW_FUNCTION("is_array");
    ALLOW_FUNCTION("is_bool");
    ALLOW_FUNCTION("is_callable");
    ALLOW_FUNCTION("is_double");
    ALLOW_FUNCTION("is_float");
    ALLOW_FUNCTION("is_int");
    ALLOW_FUNCTION("is_integer");
    ALLOW_FUNCTION("is_iterable");
    ALLOW_FUNCTION("is_long");
    ALLOW_FUNCTION("is_null");
    ALLOW_FUNCTION("is_numeric");
    ALLOW_FUNCTION("is_object");
    ALLOW_FUNCTION("is_real");
    ALLOW_FUNCTION("is_resource");
    ALLOW_FUNCTION("is_scalar");
    ALLOW_FUNCTION("is_string");
    ALLOW_FUNCTION("isset");
    ALLOW_FUNCTION("serialize");
    ALLOW_FUNCTION("settype");
    ALLOW_FUNCTION("strval");
    ALLOW_FUNCTION("unserialize");

    return SUCCESS;
}

static void ast_to_clean_dtor(zval *zv)
{
    zend_ast *ast = (zend_ast *)Z_PTR_P(zv);
    efree(ast);
}

/**
 * Request initialization lifecycle hook. Sets up the allowed functions list.
 */
int stackdriver_debugger_ast_rinit(TSRMLS_D)
{
    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_allowed_functions));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(user_allowed_functions), 8, NULL, ZVAL_PTR_DTOR, 1);

    char *ini = INI_STR(PHP_STACKDRIVER_DEBUGGER_INI_ALLOWED_FUNCTIONS);
    if (ini) {
        register_user_allowed_functions_str(ini, strlen(ini));
    }

    ALLOC_HASHTABLE(STACKDRIVER_DEBUGGER_G(ast_to_clean));
    zend_hash_init(STACKDRIVER_DEBUGGER_G(ast_to_clean), 8, NULL, ast_to_clean_dtor, 1);

    return SUCCESS;
}

/**
 * Request shutdown lifecycle hook. Cleans up the allowed functions list.
 */
int stackdriver_debugger_ast_rshutdown(TSRMLS_D)
{
    zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_allowed_functions));
    FREE_HASHTABLE(STACKDRIVER_DEBUGGER_G(user_allowed_functions));
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

    /* Setup storage for allowed functions */
    zend_hash_init(&global_allowed_functions, 1024, NULL, ZVAL_PTR_DTOR, 1);
    register_allowed_functions(&global_allowed_functions);

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
    zend_hash_destroy(&global_allowed_functions);
    zend_hash_destroy(&registered_breakpoints);

    return SUCCESS;
}

/**
 * Callback for when the user changes the allowed functions list php.ini setting.
 */
PHP_INI_MH(OnUpdate_stackdriver_debugger_allowed_functions)
{
    /* Only use this mechanism for ini_set (runtime stage) */
    if (new_value != NULL && stage & ZEND_INI_STAGE_RUNTIME) {
        zend_hash_destroy(STACKDRIVER_DEBUGGER_G(user_allowed_functions));
        zend_hash_init(STACKDRIVER_DEBUGGER_G(user_allowed_functions), 8, NULL, ZVAL_PTR_DTOR, 1);
        register_user_allowed_functions_str(ZSTR_VAL(new_value), ZSTR_LEN(new_value));
    }
    return SUCCESS;
}
