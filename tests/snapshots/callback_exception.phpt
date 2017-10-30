--TEST--
Stackdriver Debugger: Snapshot callback exception should be caught and cleared
--FILE--
<?php

function handle_snapshot($breakpoint)
{
    throw new RuntimeException('exception in snapshot handler');
}

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
  'callback' => 'handle_snapshot'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
?>
--EXPECTF--
bool(true)

Warning: stackdriver_debugger_snapshot(): Error running snapshot callback. in %s on line %d
Sum is 45
Number of breakpoints: 0
