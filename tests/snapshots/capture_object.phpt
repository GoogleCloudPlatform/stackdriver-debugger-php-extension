--TEST--
Stackdriver Debugger: Capturing an object should return a hash id
--FILE--
<?php

var_dump(stackdriver_debugger_add_snapshot('echo.php', 4));

require_once(__DIR__ . '/echo.php');

class Foo
{
    public $bar;

    public function __construct($bar)
    {
        $this->bar = $bar;
    }
}

$input = new Foo('asdf');
$output = echoValue($input);
$list = stackdriver_debugger_list_snapshots();

echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
echo "Number of stackframes: " . count($breakpoint['stackframes']) . PHP_EOL;
$echoVariables = $breakpoint['stackframes'][0]['locals'];
$rootVariables = $breakpoint['stackframes'][1]['locals'];

$count = count($echoVariables);
echo "function has $count variables" . PHP_EOL;
$array = $echoVariables[0];
$test = array_key_exists('id', $array);
echo "function variable has id: $test" . PHP_EOL;

// Find the 'input' variable
$inputVar = null;
foreach ($rootVariables as $local) {
    if ($local['name'] == 'input') {
        $inputVar = $local;
        break;
    }
}
$test = $inputVar['id'] == $array['id'];
echo "root value has same id as function value id: $test" . PHP_EOL;

?>
--EXPECTF--
bool(true)
Number of breakpoints: 1
Number of stackframes: 2
function has 1 variables
function variable has id: 1
root value has same id as function value id: 1
