--TEST--
Stackdriver Debugger: Logpoint callback
--FILE--
<?php

function logpoint_callback($level, $message) {
    echo "logpoint: $level - $message" . PHP_EOL;
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
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
logpoint: INFO - Logpoint hit!
Sum is 45
