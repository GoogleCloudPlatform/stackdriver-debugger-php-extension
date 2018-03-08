--TEST--
Stackdriver Debugger: Logpoints should not spend more than 10MB
--FILE--
<?php

$data = [];

function logpoint_callback($level, $message) {
    global $data;
    $data[] = array_fill(0, 100000, 1);
}
// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
  'callback' => 'logpoint_callback'
]));

require_once(__DIR__ . '/loop.php');
$sum = loop(10);

// Check the number of times that the logpoint_callback was executed is
// limited as it's hard to determine exact memory usage across systems
$iterations = count($data);
$test = ($iterations < 10) && ($iterations > 1);
echo "Logpoint executed fewer than 10 times: $test" . PHP_EOL;

echo "Sum is {$sum}\n";
?>
--EXPECTF--
bool(true)
Logpoint executed fewer than 10 times: 1
Sum is 45
