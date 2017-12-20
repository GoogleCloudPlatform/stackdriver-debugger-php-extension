--TEST--
Stackdriver Debugger: Snapshots should not spend more than Xms from ini_set
--FILE--
<?php

ini_set('stackdriver_debugger.max_time', '14');

function handle_snapshot($breakpoint)
{
    echo "Breakpoint hit!" . PHP_EOL;
    usleep(10000);
}

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
  'callback' => 'handle_snapshot'
]));
var_dump(stackdriver_debugger_add_snapshot('loop.php', 9, [
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
bool(true)
Breakpoint hit!
Breakpoint hit!
Sum is 45
