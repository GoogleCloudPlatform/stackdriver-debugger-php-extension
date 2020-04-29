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

#ifndef PHP_STACKDRIVER_DEBUGGER_AST_H
#define PHP_STACKDRIVER_DEBUGGER_AST_H 1

#include "php.h"

int valid_debugger_statement(zend_string *statement);
void stackdriver_debugger_ast_process(zend_ast *ast);
int stackdriver_debugger_ast_minit(INIT_FUNC_ARGS);
int stackdriver_debugger_ast_mshutdown(SHUTDOWN_FUNC_ARGS);
int stackdriver_debugger_ast_rinit(TSRMLS_D);
int stackdriver_debugger_ast_rshutdown(TSRMLS_D);
void stackdriver_list_breakpoint_ids(zval *return_value);
int stackdriver_debugger_breakpoint_injected(zend_string *filename, zend_string *breakpoint_id);

PHP_INI_MH(OnUpdate_stackdriver_debugger_whitelisted_functions);
PHP_INI_MH(OnUpdate_stackdriver_debugger_whitelisted_methods);
PHP_INI_MH(OnUpdate_stackdriver_debugger_allow_regex);

#endif /* PHP_STACKDRIVER_DEBUGGER_AST_H */
