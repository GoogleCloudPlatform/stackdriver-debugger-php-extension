--TEST--
Stackdriver Debugger: Basic variable dump
--FILE--
<?php

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, ['callback' => 'handleSnapshot']));
var_dump(stackdriver_debugger_add_snapshot('loop.php', 9, ['callback' => 'handleSnapshot']));

function handleSnapshot($breakpoint)
{
    echo "Number of stackframes: " . count($breakpoint['stackframes']) . PHP_EOL;

    foreach ($breakpoint['stackframes'] as $sf) {
        echo basename($sf['filename']) . ":" . $sf['line'] . PHP_EOL;
    }

    $loopStackframe = $breakpoint['stackframes'][0];
    var_dump($loopStackframe['locals']);
}

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

?>
--EXPECTF--
bool(true)
bool(true)
Number of stackframes: 2
loop.php:7
multiple_snapshots_callback.php:21
array(4) {
  [0]=>
  array(2) {
    ["name"]=>
    string(5) "times"
    ["value"]=>
    int(10)
  }
  [1]=>
  array(2) {
    ["name"]=>
    string(3) "sum"
    ["value"]=>
    int(0)
  }
  [2]=>
  array(2) {
    ["name"]=>
    string(1) "i"
    ["value"]=>
    int(0)
  }
  [3]=>
  array(2) {
    ["name"]=>
    string(1) "j"
    ["value"]=>
    NULL
  }
}
Number of stackframes: 2
loop.php:9
multiple_snapshots_callback.php:21
array(4) {
  [0]=>
  array(2) {
    ["name"]=>
    string(5) "times"
    ["value"]=>
    int(10)
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
    int(3)
  }
  [3]=>
  array(2) {
    ["name"]=>
    string(1) "j"
    ["value"]=>
    NULL
  }
}
Sum is 45
