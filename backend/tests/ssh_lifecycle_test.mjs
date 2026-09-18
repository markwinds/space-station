// Linux integration regression: Node >= 22, npm package ws, OpenSSH sshd and ssh-keygen required.
// Install ws outside the repo and expose it with NODE_PATH if necessary.
// node backend/tests/ssh_lifecycle_test.mjs backend/out/build/linux-release-vcpkg/space-station
// Uses isolated data, a temporary SSH host key, and ephemeral loopback ports.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const WebSocket = createRequire(import.meta.url)('ws');
import { spawn, execFileSync } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, mkdirSync, mkdtempSync, openSync, closeSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

const root = mkdtempSync(join(tmpdir(), 'space-station-ssh-lifecycle-'));
const processes = [];
const sockets = [];
const ports = new Set();
async function freePort() {
  const server = createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const port = server.address().port;
  await new Promise((done) => server.close(done));
  if (ports.has(port)) return freePort();
  ports.add(port);
  return port;
}
async function start(command, args, log) {
  const fd = openSync(join(root, log), 'w');
  const child = spawn(command, args, { stdio: ['ignore', fd, fd] });
  closeSync(fd);
  processes.push(child);
  await once(child, 'spawn');
  return child;
}
async function stop(child) {
  if (!child.pid || child.exitCode !== null || child.signalCode !== null) return;
  const exited = once(child, 'exit');
  child.kill('SIGTERM');
  await Promise.race([exited, delay(5000).then(() => { throw new Error('shutdown timed out'); })]);
}
function inbox(ws) {
  const events = [];
  let failure;
  ws.addEventListener('error', (event) => { failure = event.error || new Error('WebSocket failed'); });
  ws.addEventListener('message', ({ data }) => {
    if (typeof data === 'string') events.push(JSON.parse(data));
  });
  return async (predicate) => {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      if (failure) throw failure;
      const index = events.findIndex(predicate);
      if (index >= 0) return events.splice(index, 1)[0];
      await delay(10);
    }
    throw new Error(`WebSocket event timeout; received ${JSON.stringify(events)}`);
  };
}

try {
  const [httpPort, tlsPort, sshPort] = await Promise.all([freePort(), freePort(), freePort()]);
  execFileSync('ssh-keygen', ['-q', '-t', 'rsa', '-b', '2048', '-N', '', '-f', join(root, 'host-key')]);
  writeFileSync(join(root, 'sshd.conf'), [
    `Port ${sshPort}`, 'ListenAddress 127.0.0.1', `HostKey ${join(root, 'host-key')}`,
    `PidFile ${join(root, 'sshd.pid')}`, 'UsePAM no', 'PasswordAuthentication no',
    'KbdInteractiveAuthentication no', 'LogLevel ERROR', '',
  ].join('\n'));
  const sshd = await start('/usr/sbin/sshd', ['-D', '-e', '-f', join(root, 'sshd.conf')], 'sshd.log');
  mkdirSync(join(root, 'data'));
  writeFileSync(join(root, 'data/config.json'), JSON.stringify({
    dataPath: join(root, 'data'), port: tlsPort, httpEnabled: true, httpPort,
  }));
  const binary = join(root, 'space-station');
  copyFileSync(resolve(process.argv[2]), binary);
  const backend = await start(binary, [], 'backend.log');
  const base = `http://127.0.0.1:${httpPort}`;
  const api = async (path, method = 'GET', body) => {
    const response = await fetch(base + path, {
      method, headers: { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body), signal: AbortSignal.timeout(3000),
    });
    assert.equal(response.status, 200);
    return response.json();
  };
  for (let attempt = 0; ; attempt++) {
    try { await api('/api/tools/ssh/hosts'); break; }
    catch (error) { if (attempt >= 100) throw error; await delay(100); }
  }
  assert.equal(sshd.exitCode, null, 'temporary sshd failed to start');
  const host = { id: 'regression-host', name: 'Regression', host: '127.0.0.1', port: sshPort, username: 'test' };
  await api('/api/tools/ssh/hosts', 'PUT', { hosts: [host] });
  const connect = async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${httpPort}/api/tools/ssh/terminal`);
    sockets.push(ws);
    const receive = inbox(ws);
    await receive((event) => event.type === 'ready');
    ws.send(JSON.stringify({ type: 'connect', hostId: host.id, password: 'unused', tabId: crypto.randomUUID() }));
    const session = await receive((event) => event.type === 'session');
    await receive((event) => event.type === 'host-key');
    return { ws, receive, token: session.resumeToken };
  };
  // Rejecting a key previously called SendEvent while holding its own mutex.
  for (const mode of ['reject', 'cancel', 'http-cancel']) {
    for (let iteration = 0; iteration < 4; iteration++) {
      const { ws, receive, token } = await connect();
      if (mode === 'reject') {
        ws.send(JSON.stringify({ type: 'trust-host', trusted: false }));
        await receive((event) => event.type === 'status' && event.status === 'closed');
      }
      if (mode === 'http-cancel') {
        const result = await api('/api/tools/ssh/session/terminate', 'POST', { resumeToken: token });
        assert.equal(result.terminated, true);
        await receive((event) => event.type === 'status' && event.status === 'closed');
      } else {
        ws.send(JSON.stringify({ type: 'terminate' }));
        await receive((event) => event.type === 'terminated');
      }
      ws.send(JSON.stringify({ type: 'client-ping', timestamp: Date.now() }));
      await receive((event) => event.type === 'client-pong');
      await api('/api/tools/ssh/hosts', 'PUT', { hosts: [host] });
      assert.equal((await api('/api/tools/ssh/hosts')).hosts.length, 1);
      ws.close();
    }
    console.log(`PASS ${mode}: terminal heartbeat and host save remain responsive`);
  }
  // Shutdown must also cancel sessions waiting for trust, and drain cleanup
  // before the plugin service and configuration store are destroyed.
  await connect();
  await stop(backend);
  assert.equal(backend.exitCode, 0);
  console.log('PASS shutdown with pending host-key confirmation');
} catch (error) {
  for (const file of ['backend.log', 'sshd.log', 'data/logs/space-station.log']) {
    try { console.error(file, readFileSync(join(root, file), 'utf8')); } catch {}
  }
  throw error;
} finally {
  for (const ws of sockets) ws.close();
  for (const child of processes) {
    try { await stop(child); } catch { child.kill('SIGKILL'); await once(child, 'exit'); }
  }
  rmSync(root, { recursive: true, force: true });
}
