--TEST--
Stackdriver Debugger: Allowing a whitelisted function from ini_set
--FILE--
<?php

ini_set('stackdriver_debugger.function_whitelist', 'foo');

$statements = [
    'foo($bar)',
    'bar($foo)',
    'asdf()',
];
var_dump(ini_get('stackdriver_debugger.function_whitelist'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(3) "foo"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: false
statement: 'asdf()' valid: false
