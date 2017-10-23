<?php

function loop($times)
{
    $sum = 0;
    for ($i = 0; $i < $times; $i++) {
        $sum += $i;
        if ($i == 3) {
            $j = 1;
        }
    }
    return $sum;
}
