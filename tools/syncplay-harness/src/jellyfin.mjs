// Minimal Jellyfin HTTP client used by the harness (Node 22+: global fetch).
import { randomUUID } from 'node:crypto';

export const TICKS_PER_MS = 10_000;

export class JellyfinHttp {
  constructor(baseUrl, { client = 'SyncPlayHarness', device = 'harness', deviceId = randomUUID(), version = '0.1.0' } = {}) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.client = client;
    this.device = device;
    this.deviceId = deviceId;
    this.version = version;
    this.token = null;
    this.userId = null;
  }

  authorization() {
    const parts = [`Client="${this.client}"`, `Device="${this.device}"`, `DeviceId="${this.deviceId}"`, `Version="${this.version}"`];
    if (this.token) parts.push(`Token="${this.token}"`);
    return `MediaBrowser ${parts.join(', ')}`;
  }

  async request(method, path, body, { allowStatus = [] } = {}) {
    const res = await fetch(`${this.baseUrl}/${path.replace(/^\//, '')}`, {
      method,
      headers: { Authorization: this.authorization(), ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await res.text();
    let data = null;
    if (text) {
      try { data = JSON.parse(text); } catch { data = text; }
    }
    if (!res.ok && !allowStatus.includes(res.status)) {
      const error = new Error(`${method} ${path} -> ${res.status}: ${typeof data === 'string' ? data : JSON.stringify(data)}`);
      error.status = res.status;
      error.body = data;
      throw error;
    }
    return { status: res.status, data };
  }

  get(path, opts) { return this.request('GET', path, undefined, opts).then((r) => r.data); }
  post(path, body = {}, opts) { return this.request('POST', path, body, opts); }

  async login(username, password) {
    const { data } = await this.post('Users/AuthenticateByName', { Username: username, Pw: password });
    this.token = data.AccessToken;
    this.userId = data.User.Id;
    return data;
  }
}

export async function waitForHealth(baseUrl, timeoutMs = 180_000) {
  const deadline = Date.now() + timeoutMs;
  let last = '';
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`${baseUrl}/health`);
      last = `${res.status} ${await res.text()}`;
      if (res.ok && last.includes('Healthy')) return;
    } catch (e) {
      last = e.message;
    }
    await new Promise((r) => setTimeout(r, 1000));
  }
  throw new Error(`Server at ${baseUrl} did not become healthy: ${last}`);
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
