--TEST--
Stackdriver Debugger: Logpoint should trigger multiple times in the same request.
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/loop.php');

$sum = loop(3);

echo "Sum is {$sum}\n";

$logpoints = stackdriver_debugger_list_logpoints();

echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

var_dump($logpoints);
?>
--EXPECTF--
bool(true)
Sum is 3
Number of logpoints: 3
array(3) {
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
  [1]=>
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
  [2]=>
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
