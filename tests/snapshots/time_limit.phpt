--TEST--
Stackdriver Debugger: Snapshot callback timeout should skip later snapshots
--FILE--
<?php

function handle_snapshot($breakpoint)
{
    echo "Breakpoint hit!" . PHP_EOL;
    usleep(10000); // default max time is 10ms, use it all
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
