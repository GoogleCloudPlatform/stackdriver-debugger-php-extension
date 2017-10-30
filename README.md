# Stackdriver Debugger PHP Extension [![CircleCI](https://circleci.com/gh/GoogleCloudPlatform/stackdriver-debugger-php-extension.svg?style=svg)](https://circleci.com/gh/GoogleCloudPlatform/stackdriver-debugger-php-extension)

Stackdriver Debugger is a free, open-source way to debug your running
application without requiring a redeployment.

This library allows you to set breakpoints in your running application that
conditionally capture local variable state, stack traces, and more. This library
can work in conjunction with the PHP library
[google/cloud-debugger](https://packagist.org/packages/google/cloud-debugger) in
order to send collected data to a backend storage server.

## Compatibilty

This extension has been built and tested on the following PHP versions:

* 7.2.0 RC3
* 7.1.x (ZTS and non-ZTS)
* 7.0.x (ZTS and non-ZTS)

## Installation

### Build from source

1. [Download a release](https://github.com/GoogleCloudPlatform/stackdriver-debugger-php-extension/releases)

   ```bash
   curl https://github.com/GoogleCloudPlatform/stackdriver-debugger-php-extension/archive/v0.1.0.tar.gz -o debugger.tar.gz
   ```

1. Untar the package

   ```bash
   tar -zxvf debugger.tar.gz
   ```

1. Go to the extension directory

   ```bash
   cd stackdriver-debugger-php-extension-0.1.0/ext
   ```

1. Compile the extension

   ```bash
   phpize
   configure --enable-stackdriver-debugger
   make
   make test
   make install
   ```

1. Enable the stackdriver debugger extension. Add the following to your
   `php.ini` configuration file.

   ```
   extension=stackdriver_debugger.so
   ```

### Download from PECL (not yet available)

When this extension is available on PECL, you will be able to download and
install it easily using the `pecl` CLI tool:

```bash
pecl install stackdriver_debugger
```

## Usage

### Snapshots

A snapshot captures the current stacktrace at the specified file and line,
including all local variables at each point of the stacktrace. These snapshots
do not stop the execution of your application. Each snapshot should only be
captured ONCE (although snapshots may be captured in parallel requests).

The general workflow for using snapshots is to register the snapshot as soon as
possible in your application, then fetch the list at the end of the request for
reporting.

#### Registering Snapshots

To register a snapshot for capture, use the `stackdriver_debugger_add_snapshot`
function:

```php
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
 *      @type string $sourceRoot
 *      @type callable $callback
 * }
 */
function stackdriver_debugger_add_snapshot($filename, $line, $options);
```

#### Fetching Captured Snapshots

To retrieve all captured snapshots for this request, use the
`stackdriver_debugger_list_snapshots` function:

```php
/**
 * Return the collected list of debugger snapshots that have been collected for
 * this request.
 *
 * @return array
 */
function stackdriver_debugger_list_snapshots();
```

This function returns an array of snapshots. Each snapshot is an associative
array with the following fields:

* `id` - string - the identifier of the snapshot
* `stackframes` - array - array of stackframe data
* `evaluatedExpressions` - array - associative array of expression => expression
  result

Each stackframe is an associative array with the following fields:

* `function` - string - the current function, if any
* `filename` - string - file being executed
* `line` - string - line being executed
* `locals` array - array of local variables in the current scope

Each variable is an associative array with the following fields:

* `name` - string - the name of the local variable
* `value` - mixed - a copy of the variable at the captured point in time

### Logpoints

A logpoint creates a message for logging at the specified file and line. The
message are executed in the current execution scope to allow you to utilize
local variables. Logpionts will be executed EVERY time that file and line are
hit.

#### Registering Logpoints

To register a logpoint for capture, use the `stackdriver_debugger_add_logpoint`
function:

```php
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
 *      @type callable $callback
 * }
 */
function stackdriver_debugger_add_logpoint($filename, $line, $logLevel, $format, $options);
```

#### Fetching Captured Logpoint Messages

To retrieve all captured logpoint messages, use the
`stackdriver_debugger_list_logpoints` function:

```php
/**
 * Return the collected list of logpoint messages that have been collected for
 * this request.
 *
 * @return array
 */
function stackdriver_debugger_list_logpoints();
```

This function returns an array of messages. Each message is an array with the
following fields:

* `filename` - string - full path to file
* `line` - int - line in the file that was executed
* `message` - string - output message
* `timestamp` - int - UNIX timestamp
* `level` - string - log level

## Versioning

You can retrieve the version of this extension at runtime.

```php
/**
 * Return the current version of the stackdriver_debugger extension
 *
 * @return string
 */
function stackdriver_debugger_version();
```

This library follows [Semantic Versioning](http://semver.org/).

Please note it is currently under active development. Any release versioned
0.x.y is subject to backwards incompatible changes at any time.

**GA**: Libraries defined at a GA quality level are stable, and will not
introduce backwards-incompatible changes in any minor or patch releases. We will
address issues and requests with the highest priority.

**Beta**: Libraries defined at a Beta quality level are expected to be mostly
stable and we're working towards their release candidate. We will address issues
and requests with a higher priority.

**Alpha**: Libraries defined at an Alpha quality level are still a
work-in-progress and are more likely to get backwards-incompatible updates.

## Contributing

Contributions to this library are always welcome and highly encouraged.

See [CONTRIBUTING](CONTRIBUTING.md) for more information on how to get started.

## License

Apache 2.0 - See [LICENSE](LICENSE) for more information.
