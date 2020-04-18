--TEST--
Stackdriver Debugger: Warnings in expressions will display.
--FILE--
<?php
set_error_handler(function(int $number, string $message) {
   throw new \Error('Error')  ;
});

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_snapshot('loop.php', 7, [
    'expressions' => [
        '$times->foo == 4'
    ]
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(4);

echo "Sum is {$sum}\n";

$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;
$breakpoint = $list[0];
var_dump($breakpoint['evaluatedExpressions']);
?>
--EXPECTF--
bool(true)
Sum is 6
Number of breakpoints: 1
array(1) {
  ["$times->foo == 4"]=>
  string(19) "ERROR IN EXPRESSION"
}
