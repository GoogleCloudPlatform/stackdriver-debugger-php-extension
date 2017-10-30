--TEST--
Stackdriver Debugger: Logpoint callback exception should be caught and cleared
--FILE--
<?php

function logpoint_callback($level, $message) {
    throw new RuntimeException("error in logpoint callback");
}

// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
  'callback' => 'logpoint_callback'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

echo "Sum is {$sum}\n";
?>
--EXPECTF--
bool(true)

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d

Warning: stackdriver_debugger_logpoint(): Error running logpoint callback. in %s on line %d
Sum is 45
