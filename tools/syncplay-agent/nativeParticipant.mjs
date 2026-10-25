// Our client as a harness participant: the jfd_syncplay_agent process runs
// the app's session runtime (core, group client, API client, session
// WebSocket) against the server through a local TCP proxy. The proxy is how a
// network drop is simulated: it cuts every connection and refuses new ones,
// so the agent's own reconnect logic is what brings it back.
import { spawn } from 'node:child_process';
import net from 'node:net';
import { createInterface } from 'node:readline';
import { sleep } from '../syncplay-harness/src/jellyfin.mjs';

// Forwards TCP to the server. `setDown` cuts and refuses connections, as a
// lost network does. `freeze` is what a sleeping computer leaves behind: the
// server side of every connection is gone, while our side stays open and
// silent until our client notices on its own.
class Proxy {
  constructor(target) {
    this.target = new URL(target);
    this.pairs = new Set();
    this.down = false;
    this.server = net.createServer((client) => this.accept(client));
  }

  async listen() {
    await new Promise((resolve) => this.server.listen(0, '127.0.0.1', resolve));
    const url = new URL(this.target);
    url.hostname = '127.0.0.1';
    url.port = String(this.server.address().port);
    return url.toString().replace(/\/$/, '');
  }

  accept(client) {
    if (this.down) {
      client.destroy();
      return;
    }
    const port = Number(this.target.port || (this.target.protocol === 'https:' ? 443 : 80));
    const upstream = net.connect(port, this.target.hostname);
    const pair = { client, upstream, frozen: false };
    this.pairs.add(pair);
    for (const socket of [client, upstream]) socket.on('error', () => {});
    client.pipe(upstream);
    upstream.pipe(client);
    client.on('close', () => {
      this.pairs.delete(pair);
      upstream.destroy();
    });
    upstream.on('close', () => {
      if (!pair.frozen) client.destroy();
    });
  }

  setDown(down) {
    this.down = down;
    if (down) for (const { client } of this.pairs) client.destroy();
  }

  freeze() {
    for (const pair of this.pairs) {
      if (pair.frozen) continue;
      pair.frozen = true;
      pair.client.unpipe(pair.upstream);
      pair.upstream.unpipe(pair.client);
      pair.client.pause();
      pair.upstream.destroy();
    }
  }

  close() {
    this.setDown(true);
    this.server.close();
  }
}

export class NativeParticipant {
  /**
   * @param {string} baseUrl
   * @param {{name: string, password: string}} user
   * @param {{agent: string, label?: string, supportsRate?: boolean, speedErrorPpm?: number,
   *          log?: (msg: string) => void}} options
   */
  constructor(baseUrl, user, { agent, label = `${user.name}-native`, supportsRate = true, speedErrorPpm = 0, log = () => {} }) {
    this.kind = 'native';
    this.label = label;
    this.user = user;
    this.baseUrl = baseUrl;
    this.agentPath = agent;
    this.supportsRate = supportsRate;
    this.speedErrorPpm = speedErrorPpm;
    this.log = (msg) => log(`[${this.label}] ${msg}`);
    this.serial = 0;
    this.samples = new Map();
    this.waiters = [];
    this.events = [];
    this.connected = false;
  }

  async start() {
    this.proxy = new Proxy(this.baseUrl);
    const url = await this.proxy.listen();
    const args = ['--server', url, '--user', this.user.name, '--password', this.user.password,
      '--device', `jfd-agent-${this.label}`, '--speed-error-ppm', String(this.speedErrorPpm)];
    if (!this.supportsRate) args.push('--no-rate');
    this.agent = spawn(this.agentPath, args, {
      stdio: ['pipe', 'pipe', 'inherit'],
      env: { ...process.env, LC_ALL: process.env.LC_ALL || 'C.UTF-8' },
    });
    this.exited = new Promise((resolve) => this.agent.on('exit', resolve));
    this.agent.on('exit', (code, signal) => {
      if (!this.stopping) this.log(`agent exited (${code ?? signal})`);
    });
    createInterface({ input: this.agent.stdout }).on('line', (line) => this.onAgent(JSON.parse(line)));
    const ready = this.waitFor((e) => e.type === 'ready' || e.type === 'fatal', 30_000, 'session socket');
    const first = await ready;
    if (first.type === 'fatal') throw new Error(`${this.label}: ${first.data.text}`);
  }

  /** Cuts the network for `downMs`; resolves once it is back (the agent reconnects by itself). */
  async dropConnection(downMs) {
    this.proxy.setDown(true);
    await sleep(downMs);
    this.proxy.setDown(false);
  }

  /**
   * The computer sleeps for `ms`: the agent is frozen, and when it wakes its
   * connections are dead without knowing (the network itself works again).
   */
  async sleepFor(ms) {
    this.proxy.freeze();
    this.agent.kill('SIGSTOP');
    await sleep(ms);
    this.agent.kill('SIGCONT');
  }

  toAgent(obj) {
    if (this.agent?.exitCode == null) this.agent.stdin.write(`${JSON.stringify(obj)}\n`);
  }

  onAgent(msg) {
    switch (msg.t) {
      case 'sample':
      case 'tracks':
      case 'journal': {
        const resolve = this.samples.get(msg.id);
        this.samples.delete(msg.id);
        resolve?.(msg);
        break;
      }
      case 'log':
        this.log(msg.text);
        break;
      case 'ws':
        this.record({ type: msg.type, data: msg.data });
        break;
      case 'request':
        this.record({ type: 'request', data: { path: msg.path, status: msg.status } });
        break;
      case 'ready':
        this.connected = true;
        this.record({ type: 'ready', data: msg });
        break;
      case 'disconnected':
        this.connected = false;
        this.record({ type: 'disconnected', data: msg });
        break;
      case 'resumed':
        this.log(`woke after ${msg.sleptMs} ms`);
        this.record({ type: 'resumed', data: msg });
        break;
      case 'fatal':
        this.log(`fatal: ${msg.text}`);
        this.record({ type: 'fatal', data: msg });
        break;
      default:
        this.record({ type: msg.t, data: msg });
        break;
    }
  }

  record(event) {
    const entry = { at: Date.now(), ...event };
    this.events.push(entry);
    for (const waiter of [...this.waiters]) {
      if (waiter.match(entry)) {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        waiter.resolve(entry);
      }
    }
  }

  waitFor(match, timeoutMs = 15_000, what = 'event') {
    return new Promise((resolve, reject) => {
      const waiter = { match, resolve };
      this.waiters.push(waiter);
      setTimeout(() => {
        const i = this.waiters.indexOf(waiter);
        if (i >= 0) {
          this.waiters.splice(i, 1);
          reject(new Error(`${this.label}: timed out waiting for ${what}`));
        }
      }, timeoutMs);
    });
  }

  /** `{ at, positionMs, playing, rate }` like the other participants, plus the session's view. */
  sample() {
    const id = ++this.serial;
    return new Promise((resolve) => {
      this.samples.set(id, resolve);
      this.toAgent({ t: 'sample', id });
    });
  }

  async join(groupId) {
    const joined = this.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    const refused = this.waitFor((e) => e.type === 'request' && e.data.path === 'SyncPlay/Join' && e.data.status >= 300, 15_000, 'refusal');
    this.toAgent({ t: 'join', group: groupId });
    const first = await Promise.race([joined, refused]);
    joined.catch(() => {});
    refused.catch(() => {});
    return first.type === 'request' ? first.data.status : 204;
  }

  async createGroup(name) {
    const joined = this.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    this.toAgent({ t: 'create', name });
    return (await joined).data.GroupId;
  }

  // The app agent only (app/appAgent.cpp): the item page's Watch together,
  // the player's track menus and the segment Skip button.
  async watchTogether(itemId, name) {
    const joined = this.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    this.toAgent({ t: 'watch', item: itemId, name });
    return (await joined).data.GroupId;
  }

  tracks() {
    const id = ++this.serial;
    return new Promise((resolve) => {
      this.samples.set(id, resolve);
      this.toAgent({ t: 'tracks', id });
    });
  }

  /** The session journal's lines (app agent only): what our client heard and sent. */
  journal() {
    const id = ++this.serial;
    return new Promise((resolve) => {
      this.samples.set(id, (msg) => resolve(msg.lines ?? []));
      this.toAgent({ t: 'journal', id });
    });
  }

  selectAudio(index) { this.toAgent({ t: 'audio', index }); }
  selectSubtitle(index) { this.toAgent({ t: 'subtitle', index }); }
  skipSegment() { this.toAgent({ t: 'skip' }); }

  leave() { this.toAgent({ t: 'leave' }); }
  play() { this.toAgent({ t: 'intent', op: 'play' }); }
  pause() { this.toAgent({ t: 'intent', op: 'pause' }); }
  seek(positionMs) { this.toAgent({ t: 'intent', op: 'seek', positionMs: Math.round(positionMs) }); }
  setRate(rate) { this.toAgent({ t: 'intent', op: 'rate', rate }); }

  /** Quits as the app does on exit: the agent leaves the group first. */
  async stop() {
    if (this.stopping) return;
    this.stopping = true;
    if (this.agent && this.agent.exitCode == null) {
      this.toAgent({ t: 'quit' });
      await Promise.race([this.exited, sleep(5_000)]);
      if (this.agent.exitCode == null) this.agent.kill();
    }
    this.proxy?.close();
  }
}
