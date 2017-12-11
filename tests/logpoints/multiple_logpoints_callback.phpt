--TEST--
Stackdriver Debugger: Multiple logpoints in the same file with a callback
--FILE--
<?php

function logpoint_callback($level, $message) {
    echo "logpoint: $level - $message" . PHP_EOL;
}

// set a snapshot for line 12 in loop.php (return $sum)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
  'callback' => 'logpoint_callback'
]));
var_dump(stackdriver_debugger_add_logpoint('loop.php', 12, 'INFO', 'Logpoint 2 hit!', [
  'callback' => 'logpoint_callback'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$logpoints = stackdriver_debugger_list_logpoints();

echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

var_dump($logpoints);
?>
--EXPECTF--
bool(true)
Sum is 45
Number of logpoints: 1
array(1) {
  [0]=>
  array(5) {
    ["filename"]=>
    string(%d) "%sloop.php"
    ["line"]=>
    int(12)
    ["message"]=>
    string(13) "Logpoint hit!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
}
