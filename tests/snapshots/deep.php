<?php

// this test file enables setting a deep function stack

function depth1()
{
    $val = 1;
    return $val;
}

function depth2()
{
    $val = depth1() + 1;
    return $val;
}

function depth3()
{
    $val = depth2() + 1;
    return $val;
}

function depth4()
{
    $val = depth3() + 1;
    return $val;
}

function depth5()
{
    $val = depth4() + 1;
    return $val;
}

function depth6()
{
    $val = depth5() + 1;
    return $val;
}
