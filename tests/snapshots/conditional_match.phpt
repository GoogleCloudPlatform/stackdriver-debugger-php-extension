--TEST--
Stackdriver Debugger: Matches a conditional value breakpoint
--FILE--
<?php

// set a snapshot for line 12 in loop.php (return $sum)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 12, [
    'condition' => '$times == 4'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$sum = loop(4);

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
echo "Number of stackframes: " . count($breakpoint['stackframes']) . PHP_EOL;

$sf = $breakpoint['stackframes'][0];
var_dump($sf['locals']);
?>
--EXPECTF--
bool(true)
Number of breakpoints: 0
Number of breakpoints: 1
Number of stackframes: 2
array(4) {
  [0]=>
  array(2) {
    ["name"]=>
    string(5) "times"
    ["value"]=>
    int(4)
  }
  [1]=>
  array(2) {
    ["name"]=>
    string(3) "sum"
    ["value"]=>
    int(6)
  }
  [2]=>
  array(2) {
    ["name"]=>
    string(1) "i"
    ["value"]=>
    int(4)
  }
  [3]=>
  array(2) {
    ["name"]=>
    string(1) "j"
    ["value"]=>
    int(1)
  }
}
