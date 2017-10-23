<?php

function lineValue() {
    $line = __LINE__; // should be 4

    $line = __LINE__; // should be 6

    $line = __LINE__; // should be 8

    $line = __LINE__; // should be 10

    return $line;
}
