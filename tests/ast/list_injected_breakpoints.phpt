--TEST--
Stackdriver Debugger: Return list of injected breakpoints
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('code.php', 9, 'INFO', 'Logpoint hit!'));
var_dump(stackdriver_debugger_injected_breakpoints());
require_once(__DIR__ . '/code.php');
var_dump(stackdriver_debugger_injected_breakpoints());

?>
--EXPECTF--
bool(true)
array(0) {
}
array(1) {
  ["%s/code.php"]=>
  array(1) {
    [0]=>
    string(%d) "%d"
  }
}
