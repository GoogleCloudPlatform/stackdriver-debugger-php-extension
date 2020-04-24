--TEST--
Stackdriver Debugger: Allowing a namespaced whitelisted function. Namespaced methods should not be allowed
--INI--
stackdriver_debugger.function_whitelist="foo,Foo\Bar::asdf"
stackdriver_debugger.method_whitelist="one,One\Two::hjkl"
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
    'One\Two::hjkl()',
    '$obj->one($two)',
    '$obj->two($one)',
    '$obj->hjkl()',
    '$obj->One\Two::hjkl()',
    '$obj->Foo\Bar::asdf()',
];
var_dump(ini_get('stackdriver_debugger.function_whitelist'));
var_dump(ini_get('stackdriver_debugger.method_whitelist'));

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
string(17) "foo,Foo\Bar::asdf"
string(17) "one,One\Two::hjkl"
statement: 'foo($bar)' valid: true
statement: 'bar($foo)' valid: false
statement: 'asdf()' valid: false
statement: 'Foo\Bar::asdf()' valid: true
statement: 'One\Two::hjkl()' valid: false
statement: '$obj->one($two)' valid: true
statement: '$obj->two($one)' valid: false
statement: '$obj->hjkl()' valid: false
statement: '$obj->One\Two::hjkl()' valid: false
statement: '$obj->Foo\Bar::asdf()' valid: false
