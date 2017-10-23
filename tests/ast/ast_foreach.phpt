--TEST--
Stackdriver Debugger: Can insert in a FOREACH statement
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 48, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/code.php');

$c = new TestClass();
$c->dumpEach([
    'foo' => 'bar',
    'qwer' => 'asdf'
]);

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

?>
--EXPECTF--
bool(true)
string(14) "k: foo, v: bar"
string(16) "k: qwer, v: asdf"
Number of logpoints: 2
