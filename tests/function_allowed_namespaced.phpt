--TEST--
Stackdriver Debugger: Allowing a namespaced function
--INI--
stackdriver_debugger.functions_allowed="foo,Foo\Bar::asdf"
--FILE--
<?php

namespace Foo;

class Bar
{
    public static function asdf()
    {
        return true;
    }
}

$statements = [
    'foo($bar)',
    'bar($foo)',
    'asdf()',
    'Foo\Bar::asdf()',
];
var_dump(ini_get('stackdriver_debugger.functions_allowed'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(17) "foo,Foo\Bar::asdf"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: false
statement: 'asdf()' valid: false
statement: 'Foo\Bar::asdf()' valid: true
