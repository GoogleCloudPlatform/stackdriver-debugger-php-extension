<?php

namespace TestNamespace;

function throwRuntimeException($message)
{
    throw new \RuntimeException($message);
}

class TestClass2
{
    public $var;

    public function __construct()
    {
        $this->var = 'foo';
    }
}

namespace BracketedNamespace;

function throwRuntimeException($message)
{
    throw new \RuntimeException("Bracketed Exception: $message");
}
