--TEST--
Stackdriver Debugger: Snapshot should be able to capture expressions.
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'expressions' => [
        '"a string"',   // string
        'true',         // boolean
        '0.123',        // float
        '3 + 4',        // math expression,
        'empty("")',    // allowed AST (empty is not a function)
        '$i + 2 == 2',  // referencing variables in scope
        '$i'
    ]
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
var_dump($breakpoint['evaluatedExpressions']);
?>
--EXPECTF--
bool(true)
Sum is 45
Number of breakpoints: 1
array(7) {
  [""a string""]=>
  string(8) "a string"
  ["true"]=>
  bool(true)
  ["0.123"]=>
  float(0.123)
  ["3 + 4"]=>
  int(7)
  ["empty("")"]=>
  bool(true)
  ["$i + 2 == 2"]=>
  bool(true)
  ["$i"]=>
  int(0)
}
