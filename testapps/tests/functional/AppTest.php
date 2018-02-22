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

use Google\Cloud\Debugger\Breakpoint;
use Google\Cloud\Debugger\BreakpointStorage\FileBreakpointStorage;
use Google\Cloud\Debugger\DebuggerClient;
use PHPUnit\Framework\TestCase;
use Goutte\Client;

class AppTest extends TestCase
{
    private static $debuggee;
    private static $storage;

    private $client;

    public static function setupBeforeClass()
    {
        $client = new DebuggerClient();
        self::$debuggee = $client->debuggee('debuggeeid');
        self::$storage = new FileBreakpointStorage();
    }

    public function setUp()
    {
        // clear any breakpoints
        $this->clearBreakpoints();
        $this->client = new Client();
    }

    public function tearDown()
    {
        $this->clearBreakpoints();
    }

    public function testHomepage()
    {
        $crawler = $this->fetchPath('/');
        $this->assertEquals('Test App', $crawler->text());
    }

    public function testReadsBreakpoints()
    {
        $this->assertNumBreakpoints(0);
        $this->addBreakpoint('breakpoint1', 'web/index.php', 34);
        $this->assertNumBreakpoints(1);
    }

    public function testAddsLogpoint()
    {
        // no logpoints should be set, so no logpoint is found
        $this->fetchPath('/hello/jeff');
        $content = $this->client->getResponse()->getContent();
        $this->assertNotContains('[INFO] LOGPOINT: hello there', $content);

        // add a logpoint
        $this->addBreakpoint('logpoint1', 'web/index.php', 34, [
            'action' => Breakpoint::ACTION_LOG,
            'logMessageFormat' => 'hello there'
        ]);

        // logpoints should persist until removed
        for ($i = 0; $i < 5; $i++) {
            $this->fetchPath('/hello/jeff');
            $content = $this->client->getResponse()->getContent();
            $this->assertContains('[INFO] LOGPOINT: hello there', $content);
        }

        // remote the breakpoint
        $this->clearBreakpoints();

        // no logpoints should be set any more
        $this->fetchPath('/hello/jeff');
        $content = $this->client->getResponse()->getContent();
        $this->assertNotContains('[INFO] LOGPOINT: hello there', $content);
    }

    private function fetchPath($path, $method = 'GET')
    {
        return $this->client->request($method, 'http://localhost:9000' . $path);
    }

    private function assertNumBreakpoints($count)
    {
        $this->fetchPath('/debuggee');
        $data = json_decode($this->client->getResponse()->getContent(), true);
        $this->assertEquals($count, $data['numBreakpoints']);
    }

    private function addBreakpoint($id, $file, $line, $options = [])
    {
        list($debuggeeId, $breakpoints) = self::$storage->load();
        $breakpoints[] = new Breakpoint([
            'id' => $id,
            'location' => [
                'path' => $file,
                'line' => $line
            ]
        ] + $options);
        self::$storage->save(self::$debuggee, $breakpoints);
    }

    private function clearBreakpoints()
    {
        self::$storage->save(self::$debuggee, []);
    }
}
