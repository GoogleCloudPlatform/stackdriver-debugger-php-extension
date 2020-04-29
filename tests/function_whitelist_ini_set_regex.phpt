--TEST--
Stackdriver Debugger: Allowing a whitelisted function regex from ini_set
--FILE--
<?php

ini_set('stackdriver_debugger.function_whitelist', '/oo/');
ini_set('stackdriver_debugger.allow_regex', 1);

$statements = [
    'foo($bar)',
    'bar($foo)',
    'asdf()',
];
var_dump(ini_get('stackdriver_debugger.function_whitelist'));
var_dump(ini_get('stackdriver_debugger.allow_regex'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(4) "/oo/"
string(1) "1"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: false
statement: 'asdf()' valid: false
