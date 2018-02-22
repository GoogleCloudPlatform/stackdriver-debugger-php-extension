<?php

require __DIR__ . '/../../vendor/autoload.php';

$host = getenv('HOST') ?: 'localhost';
$port = (int)(getenv('PORT') ?: 9000);

$opcache = getenv('ENABLE_OPCACHE') == '1'
    ? '-dzend_extension=opcache.so -dopcache.enable=1'
    : '';

$command = sprintf(
    'php -S %s:%d -t web -dauto_prepend_file=prepend.php -dextension=stackdriver_debugger.so %s >/dev/null 2>&1 & echo $!',
    $host,
    $port,
    $opcache
);

$output = [];
printf('Starting web server with command: %s' . PHP_EOL, $command);
exec($command, $output);
$pid = (int) $output[0];

printf(
    '%s - Web server started on %s:%d with PID %d',
    date('r'),
    $host,
    $port,
    $pid
);

// Give the server time to boot.
sleep(1);

// Kill the web server when the process ends
register_shutdown_function(function() use ($pid) {
    echo sprintf('%s - Killing process with ID %d', date('r'), $pid) . PHP_EOL;
    exec('kill ' . $pid);
});
