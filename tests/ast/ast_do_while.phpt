--TEST--
Stackdriver Debugger: Can insert in a DO WHILE statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 56, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$c = new TestClass();
$c->doWhile(3);

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

?>
--EXPECTF--
bool(true)
Number of logpoints: 3
