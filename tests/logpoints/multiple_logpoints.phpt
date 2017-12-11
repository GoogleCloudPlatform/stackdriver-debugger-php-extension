--TEST--
Stackdriver Debugger: Multiple logpoints in the same file.
--FILE--
<?php

// set a snapshot for line 12 in loop.php (return $sum)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 12, 'INFO', 'Logpoint hit!'));
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint inner loop!'));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$logpoints = stackdriver_debugger_list_logpoints();

echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

var_dump($logpoints);
?>
--EXPECTF--
bool(true)
bool(true)
Sum is 45
Number of logpoints: 11
array(11) {
  [0]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [1]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [2]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [3]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [4]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [5]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [6]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [7]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [8]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [9]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
    ["line"]=>
    int(7)
    ["message"]=>
    string(20) "Logpoint inner loop!"
    ["timestamp"]=>
    int(%d)
    ["level"]=>
    string(4) "INFO"
  }
  [10]=>
  array(5) {
    ["filename"]=>
    string(35) "%s/loop.php"
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
