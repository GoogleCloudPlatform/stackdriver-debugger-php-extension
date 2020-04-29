--TEST--
Stackdriver Debugger: Not allowing a whitelisted function regex if regex are not enabled
--INI--
stackdriver_debugger.function_whitelist="/oo/"
--FILE--
<?php

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
string(4) "/oo/"
statement: 'foo($bar)' valid: false
statement: 'bar($foo)' valid: false
statement: 'asdf()' valid: false
