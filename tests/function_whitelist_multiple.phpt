--TEST--
Stackdriver Debugger: Allowing multiple whitelisted functions and methods
--INI--
stackdriver_debugger.function_whitelist="foo,bar"
stackdriver_debugger.method_whitelist="one,two"
--FILE--
<?php

$statements = [
    'foo($bar)',
    'bar($foo)',
    'one($two)',
    'two($one)',
    'asdf()',
    '$obj->foo($bar)',
    '$obj->bar($foo)',
    '$obj->one($two)',
    '$obj->two($one)',
];
var_dump(ini_get('stackdriver_debugger.function_whitelist'));
var_dump(ini_get('stackdriver_debugger.method_whitelist'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(7) "foo,bar"
string(7) "one,two"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: true
statement: 'one($two)' valid: false
statement: 'two($one)' valid: false
statement: 'asdf()' valid: false
statement: '$obj->foo($bar)' valid: false
statement: '$obj->bar($foo)' valid: false
statement: '$obj->one($two)' valid: true
statement: '$obj->two($one)' valid: true
