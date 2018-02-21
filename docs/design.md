# Stackdriver Debugger for PHP Design Document

## Objective

To allow PHP developers to easily view a running program's state without slowing
down the running application or requiring a redeployment using the [Stackdriver
Debugger](https://cloud.google.com/debugger). We will support PHP 7.0+.

## Background

We would like to enable setting arbitrary debug points (by file and line number)
on a deployed PHP application without redeploying. In order to accomplish this,
we must extend the PHP runtime with an extension.

PHP provides an assortment of runtime hooks that developers can use to create
their own C language-based extensions. By leveraging available hooks, we can
modify the way that PHP executes the user's code without forcing a redeployment
or even a restart.

The life of a request goes something like this:

1. Determine the .php file to execute and initialize request globals (`$_SERVER`,
   `$_REQUEST`, …)
1.  Lex/Parse/Compile the file (recursively with dependent files) into a
   `zend_op_array` struct. This may also utilize a cache mechanism. See Opcache
   Extension for more info.
1.  Execute the `zend_op_array` via `zend_execute_ex`. Class and function
   definitions are stored in per request data stores and are neither persisted
   between request nor shared between requests.

We will hook into step 2 in order to inject debugging code into the running
application. Additionally, we will need a way to signal to each request what
debug snapshots exist. When a debugged request completes, we will send all the
data to the stackdriver backend.

## Overview

The PHP implementation of Stackdriver Debugger requires 3 components:

1. A debugger daemon responsible for fetching the list of debug breakpoints and
   storing them for PHP requests to access.
1. A shared storage mechanism that the debugger daemon can write to and PHP
   requests can quickly read from.
1. A C-extension which integrates with the PHP runtime to capture variable state
   and evaluate logpoints.

## Request Lifecycle

1. As early as possible, we start an Agent instance.
   - The agent reads the list of snapshots and logpoints from the breakpoint
     store.
   - The agent registers all snapshots for capture and all logpoints for
     execution.
   - The agent registers a shutdown function to handle reporting collected data.
1. When any file is compiled, we check to see if a snapshot or logpoint is |
   present in the file.
   - If found, the code is modified to inject a php function call to evaluate
     the breakpoint. This function has a reference to the breakpoint via its
     breakpointId. This will be important for interacting with OPcache.
   - If not found, we do nothing extra.
1. When a breakpoint is "hit" (the breakpoint evaluation function is invoked),
   we look up the breakpoint config (whether capture or logpoint).
   - If there is no breakpoint found, this function will be a no-op. (Why might
     this happen?)
   - If the breakpoint has a condition set, we evaluate the condition to see if
     the result is "truthy". If not "truthy", then we do nothing. See evaluating
     conditions.
   - If the condition is not set, or evaluates to a "truthy" value, we perform
     the breakpoint behavior.
     - For snapshots, we capture local variables at each stackframe. The
       snapshot is marked as completed so we don't evaluate further.
     - For logpoints, we evaluate and store a log message.

## PHP Version Support

We will support PHP 7.0+ on 64-bit and 32-bit machines on both windows-based and
linux-based machines. PHP 5.6 will not be supported.

## PHP Library

### Stackdriver Debugger API Client

The PHP library will provide idiomatic access to the Stackdriver Debugger API.
This library is part of the `google-cloud-php` project on GitHub.

### Debugger Daemon

The PHP library will also include a `Google\Cloud\Debugger\Daemon` class as well
as a binary script to run it. The daemon is responsible for registering the node
with the Stackdriver server as a debuggee and updating the list of breakpoints
in the Breakpoint Storage. This daemon allows requests to not be blocked waiting
while fetching breakpoints from the Stackdriver server.

The debugger daemon optionally fetches its source context from the
`source-context.json` file.

### Breakpoint Storage

The daemon and request handler processes will need a way to sync the list of
breakpoints. We provide a
`Google\Cloud\Debugger\BreakpointStorage\BreakpointStorageInterface` that
abstracts this shared storage. We will provide 2 implementations for this.

#### SysvBreakpointStorage

This implementation uses shared memory to serialize and deserialize breakpoints.
This requires System V IPC family of functions which are not available on
Windows. This will be the default storage mechanism if available as it will be
much faster than reading from disk on every request.

#### FileBreakpointStorage

This implementation will utilize a shared common temporary file (in a well known
place such as `/tmp/debugger-breakpoints.json`). This will be the fallback in
case sysv is not available.

### Agent

An agent will be created for each request as early as possible
(usually in the framework's bootstrap file or even in a `auto_prepend_file`). It
will read the breakpoint configuration from the breakpoint storage and register
each breakpoint for collection. If any breakpoints are found, it will register
each breakpoint with a callback function to handle captured data. This callback
is responsible for reporting captured variables and log messages to the
Stackdriver servers.

### Distribution

This library will be distributed like the other `google/cloud` PHP veneer
libraries via PHP's standard package manager composer. It will be released
individually as `google/cloud-debugger` and will also be available through the
meta package `google/cloud`.

To install, the developer will add `google/cloud-debugger` to their
`composer.json file`. This composer package will have a dependency on the
`stackdriver_debugger` extension.

## C Extension

The debugger extension will only be responsible for modifying the runtime with
provided snapshot definitions. It will not handle communication with the
Stackdriver server.

### Registering a Breakpoint

Near the beginning of the request, you will want to register all breakpoints you
want handled in that request. This is generally handled for you by the veneer
library's Agent class. This looks something like:

```php
stackdriver_debugger_add_snapshot($file, $line, $options);
stackdriver_debugger_add_logpoint($file, $line, $logLevel, $logFormat, $options);
```

These definitions will be stored so we can look them up at compilation time in
order to modify source.

### Modifying Source at Compilation Time

In PHP all files are lexed/parsed/compiled into opcodes. Without OPCache, which
is the default, this occurs on each request for each file `require`'d during the
request. When a file is compiled, we can check the file against the list of
breakpoints to determine if a breakpoint(s) is included in that file. If it is,
we can use the `zend_ast_process` hook to modify the compiled AST. Each node in
the AST contains information about the line it's on. We can search through the
AST until we find the first statement on or after the file line and inject our
breakpoint handler into the AST.

### Injecting

To inject the breakpoint handler, we replace the AST node with a new `AST_LIST`
node that calls `stackdriver_debugger($snapshotId)` and then the original node.
This replacement will only happen in an `AST_STMT_LIST` node which are blocks of
standalone statements. This means we won't insert the breakpoints into the
middle of a multi-line statement. The `stackdriver_debugger` function will handle
evaluating the snapshot or logpoint depending on the registered configuration.

### Whitespace Handling

If we can't find an AST node on the exact line number of the breakpoint, we will
need to decide how to inject the breakpoint. If there is a statement in the same
statement list, we will pick that next line as if it were the line chosen. If
there is no statement, then we unfortunately may not be able to keep the
snapshot inside the current scope.

### Evaluating Snaphots

To evaluate a snapshot, we need to capture the current state of local variables
at each level of the stacktrace. To do this, we store structs for each
stackframe and for each stackframe, we capture a hash table (associative array)
of local variables. Each of the local variables are duplicated so that if the
value changes later, our captured values are unaffected.

If a callback is specified (automatically set up by the PHP library's Agent),
the callback is executed with the captured data. If a callback is not specified,
the results may be fetched from the extension via the PHP function:

```php
$snapshots = stackdriver_debugger_list_snapshots();
```

### Evaluating Logpoints

To evaluate a logpoint, we need to evaluate a string expression with
replacements within the current execution scope. We can accomplish this by
leveraging `zend_eval_string`.

If a callback is specified (automatically set up by the PHP library's Agent),
the callback is executed with the captured data. If a callback is not specified,
the results may be fetched from the extension via the PHP function:

```php
$messages = stackdriver_debugger_list_logpoints();
```

### Handling Conditions and Expressions

#### Validating

Conditions and expressions MUST NOT modify any execution state. Thus, we must
validate the user's input to ensure that this is the case. To validate, we will
use PHP's built-in lexing/parsing functionality to turn the provided statement
into an AST (abstract syntax tree). If the statement fails to parse, we will
handle that condition and inform the user. If parsing succeeds, we will walk the
created AST and look for any disallowed operations. If we find any disallowed
operations, we exit out and mark that this breakpoint is invalid.

Validation will happen in 2 places.

1. The daemon will validate all breakpoints it receives and validate the
   conditions and expressions. If found to be invalid, the daemon will mark the
   breakpoint as invalid and immediately update the server. Thus the requests to
   the real app never have to deal with sending the invalid breakpoints to the
   server.
1. When the setting a breakpoint, the extension will again validate the
   breakpoint because a developer could potentially use the extension without
   the daemon and setting invalid breakpoints would crash the app. In this case,
   the breakpoint is rejected with a warning and no breakpoint is set.

#### Disallowing Function Calls in Conditions and Expressions

We will disallow all function calls except those that are explicitly marked as
safe. We maintain a list of build-in functions that are whitelisted. We also
provide a `php.ini` setting that allows you to specify your own list of allowed
function calls.

### Executing

PHP provides a nice hook that extensions can use to execute PHP code:
`zend_eval_string`. This function executes the provided code and returns a PHP
user-space variable (`zval`) which we can store or operate on. It uses the
current execution scope which is exactly what we want.

```c
int zend_eval_string(zend_string *code, zval *retval, char *snippet_name);
```

#### Truthy Values

In PHP, there is a notion of "truthy" values (things that resolve to true when
used in an if statement). We will respect those semantics when deciding if a
condition is true or not. We will use the internal function
`convert_to_boolean(zval *zval)` to evaluate the truthiness of the result. This
is the same function used by php internals when casting a variable to boolean.

### Interacting with OPcache

OPcache is a heavily used C extension that is recommended for most PHP
production environments. Without OPcache, every request reads, parses, and
compiles every .php file that is required/included into an internal
`zend_op_array` (list of opcodes) that the PHP executor executes. OPcache caches
the resulting `zend_op_array` for each file (keyed by file timestamp and/or a
timeout) so that the file doesn't not need to be read, parsed or recompiled if
it hasn't changed.

Since we are effectively changing code without touching the source file, we will
need to invalidate this cache if it is enabled and a new breakpoint is set.

Also to note, OPcache's default is to have a cache for each PHP instance. Thus,
cache is not shared between multiple PHP-FPM instances. This makes syncing cache
state between requests extremely difficult.

#### Invalidating OPcache

We detect whether OPcache is available and enabled for each request by checking
for the presence of the `opcache_invalidate` function and the `php.ini` setting
`opcache.enable` (or `opcache.enable_cli` for CLI PHP environments).

Whenever a file is compiled, we maintain a list of breakpoint ids injected into
that file. This list persists between requests if OPcache is enabled.

When a request to register a breakpoint is received, we cross reference against
the list of already injected breakpoints. If the breakpoint is not yet injected,
we call `opcache_invalidate` on that file. This will force the next compilation
of that file to inject the breakpoint.

We are ok only checking for whether OPcache is enabled at the start of each
request (and caching the value), because you cannot turn on OPcache via
`ini_set` during the course of a request.

If we leave the file cached (with injected code in the AST), all that remains is
a function call to attempt to execute a breakpoint. Since the breakpoint
configuration won't exist, the call will be a no-op. The only overhead is a
function call and an internal hash table lookup.

### Distribution

This extension will be distributed via PHP's standard extension distributor
PECL. It will be released as `stackdriver_debugger`.

To install, the developer will run `pecl install stackdriver_debugger`
which will download, compile, and install the extension on the developer's
current version of PHP.

To enable the extension, the user will need to modify their `php.ini`
configuration file to include `extension=stackdriver_debugger.so`.
