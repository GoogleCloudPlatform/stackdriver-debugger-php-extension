--TEST--
Stackdriver Debugger: Return list of injected breakpoints
--FILE--
<?php

var_dump(stackdriver_debugger_add_snapshot('code.php', 10, ['snapshotId' => 'snapshotid']));
var_dump(stackdriver_debugger_add_logpoint('code.php', 9, 'INFO', 'Logpoint hit!', ['snapshotId' => 'logpointid']));
var_dump(stackdriver_debugger_injected_breakpoints());
require_once(__DIR__ . '/code.php');
var_dump(stackdriver_debugger_injected_breakpoints());

?>
--EXPECTF--
bool(true)
bool(true)
array(0) {
}
array(1) {
  ["%s/code.php"]=>
  array(2) {
    [0]=>
    string(%d) "snapshotid"
    [1]=>
    string(%d) "logpointid"
  }
}
