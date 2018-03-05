--TEST--
Stackdriver Debugger: Snapshot stackframe limit should default to 5
--FILE--
<?php

var_dump(stackdriver_debugger_add_snapshot('deep.php', 8));

require_once(__DIR__ . '/deep.php');

$depth = depth6();
var_dump($depth);

$list = stackdriver_debugger_list_snapshots();
echo "Number of breakpoints: " . count($list) . PHP_EOL;

$breakpoint = $list[0];
// var_dump($breakpoint['stackframes']);
echo "Number of stackframes: " . count($breakpoint['stackframes']) . PHP_EOL;

$i = 0;
foreach($breakpoint['stackframes'] as $stackframe) {
    $variableCount = count($stackframe['locals']);
    echo "Level $i: $variableCount variables\n";
    $i++;
}

?>
--EXPECTF--
bool(true)
int(6)
Number of breakpoints: 1
Number of stackframes: 7
Level 0: 1 variables
Level 1: 1 variables
Level 2: 1 variables
Level 3: 1 variables
Level 4: 1 variables
Level 5: 0 variables
Level 6: 0 variables
