--TEST--
Stackdriver Debugger: Can insert in a switch statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 85, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$c = new TestClass();
$c->doSwitch('bar');

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

$c->doSwitch('foo');

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

$c->doSwitch('zxcv');

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;
?>
--EXPECTF--
bool(true)
bar
Number of logpoints: 1
foo
bar
Number of logpoints: 2
default
Number of logpoints: 2
