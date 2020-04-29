--TEST--
Stackdriver Debugger: Invalid condition
--FILE--
<?php

$statements = [
    '$times = 4;',
    '$times == 4;',
    '$times + 1 == 5;',
    'echo "foo";',
    'print "foo";',
    '',
    '$times++ == 4;',
    '++$times == 4;',
    'foo($bar);',
    'true;',
    'false;',
    '$times == 4',
    // syntax errors
    'asdf->foo;',
    // whitelisted function
    'count(array([])) == 0',
    'count([]) == 0',
    'count(array_keys([])) == 0',
    'Foo\Bar::asdf()',
    '$foo->bar()',
    '$foo->asdf()',
];

foreach ($statements as $statement) {
    $valid = @stackdriver_debugger_valid_statement($statement) ? 'true' : 'false';
    echo "statement: '$statement' valid: $valid" . PHP_EOL;
}

?>
--EXPECT--
statement: '$times = 4;' valid: false
statement: '$times == 4;' valid: true
statement: '$times + 1 == 5;' valid: true
statement: 'echo "foo";' valid: false
statement: 'print "foo";' valid: false
statement: '' valid: true
statement: '$times++ == 4;' valid: false
statement: '++$times == 4;' valid: false
statement: 'foo($bar);' valid: false
statement: 'true;' valid: true
statement: 'false;' valid: true
statement: '$times == 4' valid: true
statement: 'asdf->foo;' valid: false
statement: 'count(array([])) == 0' valid: true
statement: 'count([]) == 0' valid: true
statement: 'count(array_keys([])) == 0' valid: true
statement: 'Foo\Bar::asdf()' valid: false
statement: '$foo->bar()' valid: false
statement: '$foo->asdf()' valid: false
