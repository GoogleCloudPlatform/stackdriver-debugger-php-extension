--TEST--
Stackdriver Debugger: Invalid condition yields warning when registering
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'condition' => '$times = 4;'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
?>
--EXPECTF--
Warning: stackdriver_debugger_add_snapshot(): Condition contains invalid operations in %s
bool(false)
Number of breakpoints: 0
