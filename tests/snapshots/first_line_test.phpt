--TEST--
Stackdriver Debugger: Line number test. Breakpoint is set on the first line.
of the function and it should snapshot before the local variable is set.
--FILE--
<?php

// set a snapshot for line 4 in line_numbers.php
var_dump(stackdriver_debugger_add_snapshot('line_numbers.php', 4));

require_once(__DIR__ . '/line_numbers.php');

echo lineValue() . PHP_EOL;

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
echo "Number of stackframes: " . count($breakpoint['stackframes']) . PHP_EOL;

$sf = $breakpoint['stackframes'][0];
var_dump($sf['locals']);
?>
--EXPECTF--
bool(true)
10
Number of breakpoints: 1
Number of stackframes: 2
array(1) {
  [0]=>
  array(2) {
    ["name"]=>
    string(4) "line"
    ["value"]=>
    NULL
  }
}
