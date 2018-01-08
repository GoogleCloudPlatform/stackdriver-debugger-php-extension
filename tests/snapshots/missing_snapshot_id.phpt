--TEST--
Stackdriver Debugger: Executing non-existent breakpoint is a no-op
--FILE--
<?php

var_dump(stackdriver_debugger_snapshot('non-existent-id'));
--EXPECT--
bool(false)
