--TEST--
Stackdriver Debugger: Logpoints should not spend more than 10ms
--FILE--
<?php

$count = 0;

function logpoint_callback($level, $message) {
    global $count;
    $count++;
    usleep(3000); // sleep 3ms
}
// set a snapshot for line 7 in loop.php ($sum += $i)
var_dump(stackdriver_debugger_add_logpoint('loop.php', 7, 'INFO', 'Logpoint hit!', [
  'callback' => 'logpoint_callback'
]));

require_once(__DIR__ . '/loop.php');

$sum = loop(10);

$res = $count <= 4;
echo "Ran 4 or fewer times: $res\n";
?>
--EXPECTF--
bool(true)
Ran 4 or fewer times: 1
