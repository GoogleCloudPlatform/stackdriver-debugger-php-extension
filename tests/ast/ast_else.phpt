--TEST--
Stackdriver Debugger: Can insert in an else statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 75, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$c = new TestClass();
$c->doElse(true);

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

$c->doElse(false);

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;
?>
--EXPECTF--
bool(true)
condition passed
Number of logpoints: 0
condition failed
Number of logpoints: 1
