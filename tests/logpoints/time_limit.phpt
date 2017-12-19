--TEST--
Stackdriver Debugger: Logpoints should not spend more than 10ms
--FILE--
<?php

function logpoint_callback($level, $message) {
    echo "logpoint: $level - $message" . PHP_EOL;
    usleep(10000); // default max time is 10ms, use it all
}
// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
  'callback' => 'logpoint_callback'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";
?>
--EXPECTF--
bool(true)
logpoint: INFO - Logpoint hit!
Sum is 45
