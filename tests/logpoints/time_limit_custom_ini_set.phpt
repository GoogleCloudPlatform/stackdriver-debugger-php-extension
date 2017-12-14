--TEST--
Stackdriver Debugger: Logpoints should not spend more than Xms from ini_set
--FILE--
<?php

ini_set('stackdriver_debugger.max_time', '14');

function logpoint_callback($level, $message) {
    echo "logpoint: $level - $message" . PHP_EOL;
    usleep(3000); // sleep 2ms
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
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
Sum is 45
