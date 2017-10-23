--TEST--
Stackdriver Debugger: Empty conditional is considered success
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'condition' => ''
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
?>
--EXPECTF--
bool(true)
Number of breakpoints: 1
