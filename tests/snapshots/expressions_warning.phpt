--TEST--
Stackdriver Debugger: Warnings in expressions will display.
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'expressions' => [
        '$times->foo == 4'
    ]
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(4);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
?>
--EXPECTF--
bool(true)

Notice: %s in expression evaluation on line 1
Number of breakpoints: 1
