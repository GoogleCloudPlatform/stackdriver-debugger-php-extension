--TEST--
Stackdriver Debugger: Can insert in a WHILE statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 40, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$c = new TestClass();
$c->doSomething(3);

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

?>
--EXPECTF--
bool(true)
doing something
doing something
doing something
Number of logpoints: 3
