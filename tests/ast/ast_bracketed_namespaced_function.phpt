--TEST--
Stackdriver Debugger: Can insert in a bracketed namespaced function
--FILE--
<?php

var_dump(stackdriver_debugger_add_logpoint('bracketed_namespaced_code.php', 8, 'INFO', 'Logpoint hit!'));

require_once(__DIR__ . '/bracketed_namespaced_code.php');

try {
    TestNamespace\throwRuntimeException("test message");
} catch (\RuntimeException $e) {
    echo "exception: " . $e->getMessage() . PHP_EOL;
}

$logpoints = stackdriver_debugger_list_logpoints();
echo "Number of logpoints: " . count($logpoints) . PHP_EOL;

?>
--EXPECTF--
bool(true)
exception: test message
Number of logpoints: 1
