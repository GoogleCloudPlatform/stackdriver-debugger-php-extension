# Releasing Stackdriver Debugger to PECL

1. Update the `PHP_STACKDRIVER_DEBUGGER_VERSION` package version constant in
   `php_stackdriver_debugger.h`.

1. Update the `releases.yaml` file with a new release and description.

1. Run the extension release script:

    `php scripts/ext_release.php > package.xml`

1. Build a PEAR package archive.

    `pear package`

1. Upload the new release to PECL from the [admin console][pecl-upload].

[pecl-upload]: https://pecl.php.net/release-upload.php
