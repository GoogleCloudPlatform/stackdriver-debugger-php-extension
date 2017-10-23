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

trait TestTrait
{
    function something()
    {
        echo "doing something" . PHP_EOL;
    }
}

class TestClass
{
    use TestTrait;

    private $param1;

    const DEFAULT_VALUE = 'default value';

    function __construct($param1 = null)
    {
        $this->param1 = $param1 ?: self::DEFAULT_VALUE;
    }

    function doSomething($times)
    {
        $i = 0;
        while ($i < $times) {
            $this->something();
            $i++;
        }
    }

    function dumpEach($items)
    {
        foreach ($items as $key => $value) {
            var_dump("k: $key, v: $value");
        }
    }

    function doWhile($times)
    {
        $i = 0;
        do {
            $i++;
        } while ($i < $times);
    }

    function executeClosure($times)
    {
        $closure = function ($i) {
            return $i;
        };
        for ($i = 0; $i < $times; $i++) {
            call_user_func($closure, $i);
        }
    }

    function doElse($condition)
    {
        if ($condition) {
            echo 'condition passed' . PHP_EOL;
        } else {
            echo 'condition failed' . PHP_EOL;
        }
    }

    function doSwitch($type)
    {
        switch ($type) {
            case 'foo':
                echo 'foo' . PHP_EOL;
            case 'bar':
                echo 'bar' . PHP_EOL;
                break;
            default:
                echo 'default' . PHP_EOL;
        }
    }
}
