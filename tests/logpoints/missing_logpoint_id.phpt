--TEST--
Stackdriver Debugger: Executing non-existent breakpoint is a no-op
--FILE--
<?php

var_dump(stackdriver_debugger_logpoint('non-existent-id'));
--EXPECT--
bool(false)
