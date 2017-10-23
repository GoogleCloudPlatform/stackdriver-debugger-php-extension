--TEST--
Stackdriver Debugger: Snapshot should be able to capture expressions.
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'snapshotId' => null
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
?>
--EXPECTF--
bool(true)
Sum is 45
Number of breakpoints: 1
