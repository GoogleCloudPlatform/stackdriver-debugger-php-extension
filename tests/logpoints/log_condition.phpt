--TEST--
Stackdriver Debugger: Logpoint should only evaluate when the condition is matched
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
    'condition' => '$i == 3'
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
    int(7)
    ["message"]=>
    string(13) "Logpoint hit!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
}
