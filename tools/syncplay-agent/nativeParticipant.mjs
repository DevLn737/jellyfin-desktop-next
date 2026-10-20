// Our client's SyncPlay logic (core + syncplay libraries, in the
// jfd_syncplay_agent process) as a harness participant. The agent decides
// everything; this class only carries its HTTP requests and the session
// WebSocket, the parts the Qt app's jellyfin-api module will own.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { JellyfinHttp, sleep } from '../syncplay-harness/src/jellyfin.mjs';

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
    this.agentPath = agent;
    this.supportsRate = supportsRate;
    this.speedErrorPpm = speedErrorPpm;
    this.log = (msg) => log(`[${this.label}] ${msg}`);
    this.http = new JellyfinHttp(baseUrl, { client: 'JellyfinDesktopNext', device: label });
    this.ws = null;
    this.serial = 0;
    this.samples = new Map();
    this.waiters = [];
    this.events = [];
    this.connected = false;
  }

  async start() {
    await this.http.login(this.user.name, this.user.password);
    this.agent = spawn(this.agentPath, ['--speed-error-ppm', String(this.speedErrorPpm)], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.agent.on('exit', (code, signal) => {
      if (!this.stopping) this.log(`agent exited (${code ?? signal})`);
    });
    createInterface({ input: this.agent.stdout }).on('line', (line) => this.onAgent(JSON.parse(line)));
    await this.connect();
    this.toAgent({ t: 'start' });
  }

  // Capabilities, then the WebSocket, as the app does after login or a reconnect.
  async connect() {
    await this.http.post('Sessions/Capabilities/Full', {
      PlayableMediaTypes: ['Video', 'Audio'],
      SupportedCommands: [],
      SupportsMediaControl: true,
      SupportsSyncPlayPlaybackRate: this.supportsRate,
    });
    const wsUrl = `${this.http.baseUrl.replace(/^http/, 'ws')}/socket?ApiKey=${this.http.token}&deviceId=${this.http.deviceId}`;
    const ws = new WebSocket(wsUrl);
    await new Promise((resolve, reject) => {
      ws.addEventListener('open', resolve, { once: true });
      ws.addEventListener('error', reject, { once: true });
    });
    this.ws = ws;
    this.connected = true;
    ws.addEventListener('message', (event) => this.onSocket(String(event.data)));
    ws.addEventListener('close', () => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.connected = false;
      if (!this.stopping) this.toAgent({ t: 'connection', state: 'lost' });
    });
  }

  onSocket(text) {
    const msg = JSON.parse(text);
    if (msg.MessageType === 'ForceKeepAlive') {
      this.ws?.send(JSON.stringify({ MessageType: 'KeepAlive' }));
      clearInterval(this.keepAlive);
      this.keepAlive = setInterval(() => this.ws?.send(JSON.stringify({ MessageType: 'KeepAlive' })), Math.max(5, (msg.Data ?? 60) / 2) * 1000);
      return;
    }
    if (msg.MessageType === 'SyncPlayCommand' || msg.MessageType === 'SyncPlayGroupUpdate') {
      this.record({ type: msg.MessageType, data: msg.Data });
    }
    this.toAgent({ t: 'ws', data: text });
  }

  /** Drops the WebSocket as a lost network would, then reconnects after `downMs`. */
  async dropConnection(downMs) {
    const ws = this.ws;
    this.ws = null;
    this.connected = false;
    ws?.close();
    this.toAgent({ t: 'connection', state: 'lost' });
    this.offline = true;
    await sleep(downMs);
    this.offline = false;
    await this.connect();
    this.toAgent({ t: 'connection', state: 'restored' });
  }

  toAgent(obj) {
    this.agent.stdin.write(`${JSON.stringify(obj)}\n`);
  }

  onAgent(msg) {
    switch (msg.t) {
      case 'http':
        this.forward(msg);
        break;
      case 'sample': {
        const resolve = this.samples.get(msg.id);
        this.samples.delete(msg.id);
        resolve?.(msg);
        break;
      }
      case 'log':
        this.log(msg.text);
        break;
      default:
        this.record({ type: msg.t, data: msg });
        break;
    }
  }

  async forward({ id, method, path, body }) {
    let status = 0;
    let text = '';
    if (!this.offline) {
      try {
        const res = await fetch(`${this.http.baseUrl}/${path.replace(/^\//, '')}`, {
          method,
          headers: { Authorization: this.http.authorization(), ...(body ? { 'Content-Type': 'application/json' } : {}) },
          body: method === 'GET' ? undefined : body || undefined,
        });
        status = res.status;
        text = await res.text();
      } catch (e) {
        this.log(`${method} ${path} failed: ${e.message}`);
      }
    }
    if (path.startsWith('SyncPlay/') && path !== 'SyncPlay/Ping') this.record({ type: 'request', data: { path, status } });
    this.toAgent({ t: 'reply', id, status, body: text });
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
    const first = await Promise.race([joined, refused]).catch((e) => { throw e; });
    joined.catch(() => {});
    refused.catch(() => {});
    return first.type === 'request' ? first.data.status : 204;
  }

  async createGroup(name) {
    const joined = this.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    this.toAgent({ t: 'create', name });
    return (await joined).data.GroupId;
  }

  leave() { this.toAgent({ t: 'leave' }); }
  play() { this.toAgent({ t: 'intent', op: 'play' }); }
  pause() { this.toAgent({ t: 'intent', op: 'pause' }); }
  seek(positionMs) { this.toAgent({ t: 'intent', op: 'seek', positionMs: Math.round(positionMs) }); }
  setRate(rate) { this.toAgent({ t: 'intent', op: 'rate', rate }); }

  async stop() {
    this.stopping = true;
    clearInterval(this.keepAlive);
    if (this.agent && this.agent.exitCode == null) {
      this.toAgent({ t: 'leave' });
      await sleep(200);
      this.toAgent({ t: 'quit' });
    }
    this.ws?.close();
    await sleep(50);
  }
}
