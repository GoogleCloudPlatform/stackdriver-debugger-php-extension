--TEST--
Stackdriver Debugger: Can insert in a FOR statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 7, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$sum = loop(10);

echo "Sum is {$sum}\n";

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

?>
--EXPECTF--
bool(true)
Sum is 45
Number of logpoints: 10
