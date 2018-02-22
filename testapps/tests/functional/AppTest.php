<?php
/**
 * Copyright 2018 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

use Google\Cloud\Debugger\BreakpointStorage\FileBreakpointStorage;
use Google\Cloud\Debugger\DebuggerClient;
use PHPUnit\Framework\TestCase;
use Goutte\Client;

class AppTest extends TestCase
{
    public static function setupBeforeClass()
    {
        $client = new DebuggerClient();
        $debuggee = $client->debuggee('debuggeeid');
        $storage = new FileBreakpointStorage();
        $storage->save($debuggee, []);
    }

    public function testHomepage()
    {
        $client = new Client();
        $crawler = $client->request('GET', 'http://localhost:9000/');
        $this->assertEquals('Test App', $crawler->text());
    }
}
