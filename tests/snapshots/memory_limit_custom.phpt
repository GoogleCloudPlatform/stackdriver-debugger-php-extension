--TEST--
Stackdriver Debugger: Snapshots should not spend more than X MB
--INI--
stackdriver_debugger.max_memory=1
--FILE--
<?php

$data = [];

function handle_snapshot($breakpoint)
{
    global $data;
    echo "Breakpoint hit!" . PHP_EOL;
    $data[] = array_fill(0, 100000, "abcdefghij"); // 1MB per execution
}

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
  'callback' => 'handle_snapshot'
]));
var_dump(stackdriver_debugger_add_snapshot('loop.php', 12, [
  'callback' => 'handle_snapshot'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";
?>
--EXPECTF--
bool(true)
bool(true)
Breakpoint hit!
Sum is 45
