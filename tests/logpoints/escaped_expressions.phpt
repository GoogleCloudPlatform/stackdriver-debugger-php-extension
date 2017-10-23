--TEST--
Stackdriver Debugger: Escaped expressions should not be replaced.
--FILE--
<?php

// set a snapshot for line 12 in loop.php (return $sum)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 12, 'INFO', 'Logpoint hit! 0: $0, $$0', [
    'expressions' => [
        '3 + 3'
    ]
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$logpoints = stackdriver_debugger_list_logpoints();

echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

var_dump(array_map(function ($logpoint) {
    return $logpoint['message'];
}, $logpoints))
?>
--EXPECTF--
bool(true)
Sum is 45
Number of logpoints: 1
array(1) {
  [0]=>
  string(%d) "Logpoint hit! 0: 6, $$0"
}
