// A SyncPlay participant running the forked Jellyfin Web in headless Chromium.
// Login goes through the real login form; group requests go through the page's
// own ApiClient (so they come from the Web session); the position is read
// straight from the <video> element, independent of Web's SyncPlay internals.

export class WebParticipant {
  /**
   * @param {import('playwright').Browser} browser
   * @param {string} baseUrl
   * @param {{name: string, password: string}} user
   * @param {{label?: string, syncCorrection?: boolean, log?: (msg: string) => void}} [options]
   */
  constructor(browser, baseUrl, user, { label = `${user.name}-web`, syncCorrection = true, log = () => {} } = {}) {
    this.kind = 'web';
    this.browser = browser;
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.user = user;
    this.label = label;
    this.syncCorrection = syncCorrection;
    this.log = (msg) => log(`[${this.label}] ${msg}`);
    this.consoleLines = [];
  }

  async start() {
    this.context = await this.browser.newContext({ viewport: { width: 960, height: 540 } });
    const correction = String(this.syncCorrection);
    await this.context.addInitScript((value) => {
      try {
        localStorage.setItem('syncPlay-enableSyncCorrection', value);
      } catch { /* storage unavailable */ }
    }, correction);
    this.page = await this.context.newPage();
    this.page.on('console', (msg) => {
      const text = msg.text();
      if (/SyncPlay|SpeedToSync|SkipToSync|playback|buffer/i.test(text)) {
        this.consoleLines.push({ at: Date.now(), type: msg.type(), text: text.slice(0, 500) });
      }
    });
    this.page.on('pageerror', (err) => this.consoleLines.push({ at: Date.now(), type: 'pageerror', text: String(err).slice(0, 500) }));
    await this.page.goto(`${this.baseUrl}/web/#/login`, { waitUntil: 'domcontentloaded' });
    const manual = this.page.locator('.btnManual');
    await this.page.waitForSelector('.manualLoginForm, .btnManual', { timeout: 60_000 });
    if (await manual.isVisible().catch(() => false)) await manual.click();
    await this.page.fill('#txtManualName', this.user.name);
    await this.page.fill('#txtManualPassword', this.user.password);
    await this.page.locator('.manualLoginForm button[type=submit]').click();
    await this.page.waitForFunction(() => window.ApiClient?.accessToken?.() && location.hash.startsWith('#/home'), null, { timeout: 60_000 });
    // Give the WebSocket a moment; SyncPlay commands arrive only through it.
    await this.page.waitForTimeout(1500);
    this.log('logged in');
  }

  /** Calls a SyncPlay endpoint as this Web session. Returns the HTTP status. */
  async request(path, body = {}) {
    return this.page.evaluate(async ({ path, body }) => {
      const api = window.ApiClient;
      const res = await fetch(api.getUrl(path), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Authorization: api.getAuthorizationHeaderValue?.() ?? `MediaBrowser Token="${api.accessToken()}"` },
        body: JSON.stringify(body),
      });
      return res.status;
    }, { path, body });
  }

  /** Joins through the page's session; Web then follows the group by itself. */
  join(groupId) { return this.request('SyncPlay/Join', { GroupId: groupId }); }

  leave() { return this.request('SyncPlay/Leave'); }
  pause() { return this.request('SyncPlay/Pause'); }
  unpause() { return this.request('SyncPlay/Unpause'); }
  seek(positionMs) { return this.request('SyncPlay/Seek', { PositionTicks: Math.round(positionMs * 10_000) }); }
  setPlaybackRate(rate) { return this.request('SyncPlay/SetPlaybackRate', { PlaybackRate: rate }); }

  async sample() {
    return this.page.evaluate(() => {
      const v = document.querySelector('video');
      const at = Date.now();
      if (!v || !v.src && !v.currentSrc) return { at, positionMs: null, playing: false, rate: 0, baseRate: null };
      const playing = !v.paused && !v.ended && v.readyState > 2;
      return { at, positionMs: v.currentTime * 1000, playing, rate: playing ? v.playbackRate : 0, baseRate: v.playbackRate };
    });
  }

  async screenshot(path) {
    await this.page.screenshot({ path });
  }

  async stop() {
    await this.leave().catch(() => {});
    await this.context?.close();
  }
}
