--TEST--
Stackdriver Debugger: Allowing multiple functions
--INI--
stackdriver_debugger.functions_allowed="foo,bar"
--FILE--
<?php

$statements = [
    'foo($bar)',
    'bar($foo)',
    'asdf()',
];
var_dump(ini_get('stackdriver_debugger.functions_allowed'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(7) "foo,bar"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: true
statement: 'asdf()' valid: false
