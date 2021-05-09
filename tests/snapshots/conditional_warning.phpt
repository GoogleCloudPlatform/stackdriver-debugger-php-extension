--TEST--
Stackdriver Debugger: Warnings in conditionals will display.
--FILE--
<?php

// set a snapshot for line 12 in loop.php (return $sum)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 12, [
    'condition' => '$times->foo == 4;'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(4);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
?>
--EXPECTF--
bool(true)

%s: %s in conditional on line 1
Number of breakpoints: 0
