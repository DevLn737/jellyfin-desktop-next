// A headless SyncPlay participant that speaks the server protocol directly and
// "plays" the media on its own clock. It is the reference for a native client:
// commands are applied at their server `When` (converted with an NTP-style
// clock offset), Seek and rate changes are answered with Ready, and the
// position is exact, so it doubles as a drift reference and a group driver.
import { JellyfinHttp, TICKS_PER_MS, sleep } from './jellyfin.mjs';

export class ProtocolParticipant {
  /**
   * @param {string} baseUrl
   * @param {{name: string, password: string}} user
   * @param {{label?: string, supportsRate?: boolean, log?: (msg: string) => void}} [options]
   */
  constructor(baseUrl, user, { label = `${user.name}-protocol`, supportsRate = true, log = () => {} } = {}) {
    this.kind = 'protocol';
    this.label = label;
    this.user = user;
    this.supportsRate = supportsRate;
    this.log = (msg) => log(`[${this.label}] ${msg}`);
    this.http = new JellyfinHttp(baseUrl, { client: 'SyncPlayHarness', device: label });
    this.ws = null;
    this.offsetMs = 0; // server - local
    this.rttMs = 0;
    this.events = [];
    this.waiters = [];
    this.timers = new Set();
    this.groupId = null;
    this.playlistItemId = null;
    this.lastEmittedAt = null;
    // Simulated player: position = anchorPos + (now - anchorAt) * rate while playing.
    this.player = { playing: false, anchorPosMs: 0, anchorAtMs: Date.now(), rate: 1 };
  }

  async start() {
    await this.http.login(this.user.name, this.user.password);
    await this.http.post('Sessions/Capabilities/Full', {
      PlayableMediaTypes: ['Video', 'Audio'],
      SupportedCommands: [],
      SupportsMediaControl: true,
      SupportsSyncPlayPlaybackRate: this.supportsRate,
    });
    await this.openSocket();
    await this.syncClock(8);
    this.clockTimer = setInterval(() => this.syncClock(3).catch(() => {}), 10_000);
  }

  async openSocket() {
    const wsUrl = `${this.http.baseUrl.replace(/^http/, 'ws')}/socket?ApiKey=${this.http.token}&deviceId=${this.http.deviceId}`;
    this.ws = new WebSocket(wsUrl);
    await new Promise((resolve, reject) => {
      this.ws.addEventListener('open', resolve, { once: true });
      this.ws.addEventListener('error', reject, { once: true });
    });
    this.ws.addEventListener('message', (event) => this.onMessage(JSON.parse(event.data)));
  }

  async syncClock(samples) {
    let best = null;
    for (let i = 0; i < samples; i++) {
      const t0 = Date.now();
      const data = await this.http.get('GetUtcTime');
      const t3 = Date.now();
      const t1 = Date.parse(data.RequestReceptionTime);
      const t2 = Date.parse(data.ResponseTransmissionTime);
      const rtt = (t3 - t0) - (t2 - t1);
      const offset = ((t1 - t0) + (t2 - t3)) / 2;
      if (!best || rtt < best.rtt) best = { rtt, offset };
    }
    this.offsetMs = best.offset;
    this.rttMs = best.rtt;
    await this.http.post('SyncPlay/Ping', { Ping: Math.round(best.rtt / 2) }).catch(() => {});
  }

  serverNow() { return Date.now() + this.offsetMs; }
  toLocal(serverIso) { return Date.parse(serverIso) - this.offsetMs; }

  onMessage(msg) {
    const { MessageType: type, Data: data } = msg;
    if (type === 'ForceKeepAlive') {
      this.send({ MessageType: 'KeepAlive' });
      clearInterval(this.keepAlive);
      this.keepAlive = setInterval(() => this.send({ MessageType: 'KeepAlive' }), Math.max(5, (data ?? 60) / 2) * 1000);
      return;
    }
    if (type !== 'SyncPlayCommand' && type !== 'SyncPlayGroupUpdate') return;
    const event = { at: Date.now(), type, data };
    this.events.push(event);
    if (type === 'SyncPlayGroupUpdate') this.onGroupUpdate(data);
    else this.onCommand(data);
    for (const waiter of [...this.waiters]) {
      if (waiter.match(event)) {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        waiter.resolve(event);
      }
    }
  }

  send(obj) {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(obj));
  }

  /** Resolves with the first future event that matches. */
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

  onGroupUpdate(update) {
    switch (update.Type) {
      case 'GroupJoined':
        this.groupId = update.GroupId;
        break;
      case 'GroupLeft':
      case 'NotInGroup':
        this.groupId = null;
        this.stopPlayer();
        break;
      case 'PlayQueue': {
        const pq = update.Data;
        const item = pq.Playlist?.[pq.PlayingItemIndex];
        if (typeof pq.PlaybackRate === 'number') this.player.rate = pq.PlaybackRate;
        if (!item) { this.playlistItemId = null; break; }
        const changed = item.PlaylistItemId !== this.playlistItemId;
        this.playlistItemId = item.PlaylistItemId;
        if (changed || pq.Reason === 'NewPlaylist' || pq.Reason === 'SetCurrentItem') {
          // "Load" the item paused at the group's start position, then report Ready.
          this.setPlayer(false, pq.StartPositionTicks / TICKS_PER_MS);
          this.schedule(150, () => this.reportReady(false));
        }
        break;
      }
      default:
        break;
    }
  }

  onCommand(cmd) {
    if (cmd.EmittedAt && this.lastEmittedAt && Date.parse(cmd.EmittedAt) <= this.lastEmittedAt) return;
    if (cmd.EmittedAt) this.lastEmittedAt = Date.parse(cmd.EmittedAt);
    if (typeof cmd.PlaybackRate === 'number') this.player.rate = cmd.PlaybackRate;
    const posMs = cmd.PositionTicks / TICKS_PER_MS;
    const whenLocal = this.toLocal(cmd.When);
    switch (cmd.Command) {
      case 'Unpause': {
        // Start at When; if When already passed, extrapolate where the group is.
        const delay = whenLocal - Date.now();
        if (delay > 0) this.schedule(delay, () => this.setPlayer(true, posMs, whenLocal));
        else this.setPlayer(true, posMs, whenLocal);
        break;
      }
      case 'Pause':
        this.schedule(Math.max(0, whenLocal - Date.now()), () => this.setPlayer(false, posMs));
        break;
      case 'Seek':
        this.setPlayer(false, posMs);
        this.schedule(100, () => this.reportReady(false));
        break;
      case 'Stop':
        this.stopPlayer();
        break;
      default:
        break;
    }
  }

  schedule(ms, fn) {
    const t = setTimeout(() => { this.timers.delete(t); fn(); }, ms);
    this.timers.add(t);
  }

  setPlayer(playing, posMs, anchorAtMs = Date.now()) {
    this.player = { ...this.player, playing, anchorPosMs: posMs, anchorAtMs };
  }

  stopPlayer() { this.setPlayer(false, 0); }

  /** Current media position in ms and whether it is playing (exact, no sampling noise). */
  async sample() {
    const now = Date.now();
    const p = this.player;
    const pos = p.playing ? p.anchorPosMs + Math.max(0, now - p.anchorAtMs) * p.rate : p.anchorPosMs;
    return { at: now, positionMs: pos, playing: p.playing, rate: p.playing ? p.rate : 0, baseRate: p.rate };
  }

  async reportReady(isPlaying) {
    const s = await this.sample();
    return this.http.post('SyncPlay/Ready', {
      When: new Date(this.serverNow()).toISOString(),
      PositionTicks: Math.round(s.positionMs * TICKS_PER_MS),
      IsPlaying: isPlaying,
      PlaylistItemId: this.playlistItemId,
    });
  }

  // --- Group requests ---------------------------------------------------
  async createGroup(name) {
    const joined = this.waitFor((e) => e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    const { data } = await this.http.post('SyncPlay/New', { GroupName: name });
    await joined;
    return data.GroupId;
  }

  async join(groupId) {
    const joined = this.waitFor((e) => e.data?.Type === 'GroupJoined', 15_000, 'GroupJoined');
    const res = await this.http.post('SyncPlay/Join', { GroupId: groupId }, { allowStatus: [409] });
    if (res.status === 409) return res;
    await joined;
    return res;
  }

  leave() { return this.http.post('SyncPlay/Leave'); }
  setNewQueue(itemIds, startMs = 0) {
    return this.http.post('SyncPlay/SetNewQueue', { PlayingQueue: itemIds, PlayingItemPosition: 0, StartPositionTicks: Math.round(startMs * TICKS_PER_MS) });
  }
  unpause() { return this.http.post('SyncPlay/Unpause'); }
  pause() { return this.http.post('SyncPlay/Pause'); }
  seek(positionMs) { return this.http.post('SyncPlay/Seek', { PositionTicks: Math.round(positionMs * TICKS_PER_MS) }); }
  setPlaybackRate(rate) { return this.http.post('SyncPlay/SetPlaybackRate', { PlaybackRate: rate }, { allowStatus: [400, 409] }); }

  async stop() {
    clearInterval(this.clockTimer);
    clearInterval(this.keepAlive);
    for (const t of this.timers) clearTimeout(t);
    if (this.groupId) await this.leave().catch(() => {});
    this.ws?.close();
    await sleep(50);
  }
}
