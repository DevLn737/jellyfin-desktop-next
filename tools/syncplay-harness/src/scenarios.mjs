// SyncPlay scenarios. Each one builds its own group so that failures stay local.
// Participants: one ProtocolParticipant drives the group and is the exact
// reference; Web participants are the real forked Jellyfin Web.
import { ProtocolParticipant } from './protocolClient.mjs';
import { WebParticipant } from './webParticipant.mjs';
import { snapshot, waitUntil, allPlaying, allPaused, round } from './measure.mjs';
import { writeFile } from 'node:fs/promises';
import { JellyfinHttp, sleep } from './jellyfin.mjs';
import { ADMIN } from './bootstrap.mjs';

export const THRESHOLDS = {
  // desktop-api-contract.json integratedAcceptanceCases: drift < 400 ms after 10/15/20 s.
  driftMs: 400,
  driftCheckpointsS: [10, 15, 20],
  // Everyone must follow a command within this time.
  propagationMs: 3000,
  // Paused participants must agree on the position within this spread.
  pausedSpreadMs: 400,
};

// Failures that are already understood and reported. They are shown in the
// report but do not fail the run, so the harness can gate PRs today; when one
// starts passing, the report says so and the entry should be removed.
export const KNOWN_ISSUES = {
  pausedSeekBuffering: 'Web 12.2: Chromium fires "waiting" on a seek while paused; SyncPlay HtmlVideoPlayer '
    + 'reports Buffering 3 s later and only clears it on "playing", so the group stays in Waiting and Unpause is ignored.',
  webSocketLegacyAuth: 'Server 12.2 sets EnableLegacyAuthorization=false (migration DisableLegacyAuthorization), but '
    + 'Web 12.2 uses jellyfin-apiclient 1.11.0, which opens the WebSocket with ?api_key= and gets HTTP 403. '
    + 'Web SyncPlay only works on servers where legacy authorization is turned back on.',
  rejoinIgnoresQueue: 'Web 12.2: after Leave and Join in the same page, QueueCore drops the group\'s PlayQueue as '
    + '"ignoring old update" (same LastUpdate as before leaving), never reports Ready, and the whole group stays in Waiting.',
};

function check(ctx, name, passed, detail, knownIssue = null) {
  const ok = Boolean(passed);
  ctx.checks.push({ name, passed: ok || Boolean(knownIssue), knownIssue: !ok && knownIssue ? knownIssue : null, knownIssueFixed: ok && knownIssue ? knownIssue : null, detail });
  ctx.log(`${ok ? '  ok ' : knownIssue ? '  KNOWN' : '  FAIL'} ${name}: ${detail}`);
}

function describe(snap) {
  return snap.samples.map((s) => `${s.label}=${s.projectedMs == null ? 'n/a' : (s.projectedMs / 1000).toFixed(3)}s${s.playing ? `@${round(s.rate, 2)}x` : ' paused'}`).join(', ');
}

/** Creates a group with a protocol driver and `webCount` Web participants, playing the test clip. */
async function startGroup(ctx, { webCount = 2, startMs = 0, syncCorrection = true } = {}) {
  const [driverUser, ...others] = ctx.users;
  const driver = new ProtocolParticipant(ctx.baseUrl, driverUser, { log: ctx.log });
  await driver.start();
  ctx.cleanup.push(() => driver.stop());
  const groupId = await driver.createGroup(`harness-${Date.now()}`);
  const webs = [];
  for (const user of others.slice(0, webCount)) {
    const web = new WebParticipant(ctx.browser, ctx.baseUrl, user, { log: ctx.log, syncCorrection });
    await web.start();
    ctx.cleanup.push(() => web.stop());
    ctx.cleanup.push(() => writeFile(`${ctx.outDir}/${ctx.name}-${web.label}-console.json`, JSON.stringify(web.consoleLines, null, 1)));
    const status = await web.join(groupId);
    if (status >= 300) throw new Error(`${web.label} could not join: HTTP ${status}`);
    webs.push(web);
  }
  const all = [driver, ...webs];
  await driver.setNewQueue([ctx.server.itemId], startMs);
  // Wait until every Web participant has the item loaded, then start the group.
  const loaded = await waitUntil(all, (s) => s.samples.every((x) => x.positionMs != null), 60_000, 250);
  if (!loaded.ok) throw new Error(`Media did not load everywhere: ${describe(loaded.snapshot)}`);
  const unpauseAt = Date.now();
  await driver.unpause();
  const playing = await waitUntil(all, allPlaying, 30_000);
  check(ctx, 'group starts playing', playing.ok, `${playing.ok ? `all playing ${playing.elapsedMs} ms after Unpause` : 'not everyone started'}; ${describe(playing.snapshot)}`);
  ctx.metrics.startLatencyMs = Date.now() - unpauseAt;
  return { driver, webs, all, groupId };
}

async function measureDrift(ctx, all, label, sinceMs) {
  const results = [];
  for (const checkpoint of THRESHOLDS.driftCheckpointsS) {
    const wait = sinceMs + checkpoint * 1000 - Date.now();
    if (wait > 0) await sleep(wait);
    const snap = await snapshot(all);
    const ok = snap.spreadMs != null && snap.spreadMs < THRESHOLDS.driftMs && snap.samples.every((s) => s.playing);
    results.push({ checkpoint, spreadMs: round(snap.spreadMs), samples: snap.samples });
    check(ctx, `${label}: drift after ${checkpoint} s`, ok, `spread ${round(snap.spreadMs)} ms (limit ${THRESHOLDS.driftMs}); ${describe(snap)}`);
  }
  ctx.metrics[`drift_${label}`] = results.map((r) => ({ checkpointS: r.checkpoint, spreadMs: r.spreadMs }));
  return results;
}

async function changeRate(ctx, group, rate) {
  const res = await group.driver.setPlaybackRate(rate);
  check(ctx, `SetPlaybackRate ${rate} accepted`, res.status === 204, `HTTP ${res.status}`);
  // Rate change = group Seek + Ready barrier + Unpause at the new rate.
  await sleep(500);
  const playing = await waitUntil(group.all, (s) => allPlaying(s) && s.samples.every((x) => Math.abs(x.rate - rate) < 0.15), 20_000);
  check(ctx, `all resume at ${rate}x`, playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`);
  return Date.now();
}

export const scenarios = [
  {
    name: 'websocket-auth',
    title: 'WebSocket authentication as Jellyfin Web 12.2 does it (?api_key=) with legacy authorization off and on',
    async run(ctx) {
      const admin = new JellyfinHttp(ctx.baseUrl, { device: 'harness-admin' });
      await admin.login(ADMIN.name, ADMIN.password);
      const user = new JellyfinHttp(ctx.baseUrl, { device: 'harness-ws' });
      await user.login(ctx.users[1].name, ctx.users[1].password);
      const config = await admin.get('System/Configuration');
      const tryOpen = (query) => new Promise((resolve) => {
        const ws = new WebSocket(`${ctx.baseUrl.replace(/^http/, 'ws')}/socket?${query}=${user.token}&deviceId=${user.deviceId}`);
        ws.addEventListener('open', () => { ws.close(); resolve(true); }, { once: true });
        ws.addEventListener('error', () => resolve(false), { once: true });
      });
      try {
        await admin.post('System/Configuration', { ...config, EnableLegacyAuthorization: false });
        check(ctx, 'ApiKey= opens the socket with legacy authorization off', await tryOpen('ApiKey'), '12.2 default configuration');
        check(ctx, 'api_key= (what Web 12.2 sends) opens the socket with legacy authorization off', await tryOpen('api_key'),
          'without a socket Web receives no SyncPlay commands', KNOWN_ISSUES.webSocketLegacyAuth);
        await admin.post('System/Configuration', { ...config, EnableLegacyAuthorization: true });
        check(ctx, 'api_key= opens the socket with legacy authorization on', await tryOpen('api_key'), 'the harness runs with this setting');
      } finally {
        await admin.post('System/Configuration', config);
      }
    },
  },
  {
    name: 'drift-1x',
    title: 'Mixed group at 1x: start latency and drift at 10/15/20 s',
    async run(ctx) {
      const group = await startGroup(ctx);
      await measureDrift(ctx, group.all, '1x', Date.now());
    },
  },
  {
    name: 'drift-rates',
    title: 'Group rate 1.25x and 1.5x while playing, drift after each change, back to 1x',
    async run(ctx) {
      const group = await startGroup(ctx);
      for (const rate of [1.25, 1.5]) {
        const since = await changeRate(ctx, group, rate);
        await measureDrift(ctx, group.all, `${rate}x`, since);
      }
      await changeRate(ctx, group, 1.0);
    },
  },
  {
    name: 'pause-seek',
    title: 'Pause propagation, seek to 30 s while paused, rate change while paused keeps pause, resume',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(5000);
      // Pause requested by a Web participant: tests Web -> server -> everyone.
      const t0 = Date.now();
      const status = await group.webs[0].pause();
      check(ctx, 'Web pause request accepted', status === 204, `HTTP ${status}`);
      let paused = await waitUntil(group.all, allPaused, THRESHOLDS.propagationMs * 2);
      check(ctx, 'everyone paused', paused.ok, `${paused.elapsedMs} ms; ${describe(paused.snapshot)}`);
      await sleep(1000);
      let snap = await snapshot(group.all);
      check(ctx, 'paused positions agree', snap.spreadMs != null && snap.spreadMs < THRESHOLDS.pausedSpreadMs, `spread ${round(snap.spreadMs)} ms; ${describe(snap)}`);
      ctx.metrics.pauseLatencyMs = paused.elapsedMs;
      ctx.metrics.pausedSpreadMs = round(snap.spreadMs);

      await group.driver.seek(30_000);
      const seeked = await waitUntil(group.all, (s) => allPaused(s) && s.samples.every((x) => Math.abs(x.positionMs - 30_000) < THRESHOLDS.pausedSpreadMs), 15_000);
      check(ctx, 'seek to 30 s while paused lands everywhere and stays paused', seeked.ok, `${seeked.elapsedMs} ms; ${describe(seeked.snapshot)}`);
      await sleep(3000);
      snap = await snapshot(group.all);
      check(ctx, 'still paused 3 s after seek', allPaused(snap), describe(snap));

      const res = await group.driver.setPlaybackRate(1.5);
      check(ctx, 'rate change while paused accepted', res.status === 204, `HTTP ${res.status}`);
      await sleep(4000);
      snap = await snapshot(group.all);
      check(ctx, 'rate change while paused keeps everyone paused', allPaused(snap), describe(snap));

      const resumeAt = Date.now();
      await group.driver.unpause();
      const playing = await waitUntil(group.all, (s) => allPlaying(s) && s.samples.every((x) => Math.abs(x.rate - 1.5) < 0.15), 20_000);
      // A rate change is a group Seek, so it hits the same Web buffering bug as a paused seek.
      check(ctx, 'resume at 1.5x after paused rate change', playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`, KNOWN_ISSUES.pausedSeekBuffering);
      if (playing.ok) {
        ctx.metrics.unpauseLatencyMs = playing.elapsedMs;
        await measureDrift(ctx, group.all, 'after-paused-rate-1.5x', resumeAt);
      }
    },
  },
  {
    name: 'seek-paused-resume',
    title: 'Pause, seek while paused, wait 5 s, resume: the common "jump and press play" flow',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(3000);
      await group.driver.pause();
      const paused = await waitUntil(group.all, allPaused, THRESHOLDS.propagationMs);
      check(ctx, 'everyone paused', paused.ok, `${paused.elapsedMs} ms; ${describe(paused.snapshot)}`);
      await group.driver.seek(60_000);
      const seeked = await waitUntil(group.all, (s) => allPaused(s) && s.samples.every((x) => Math.abs(x.positionMs - 60_000) < THRESHOLDS.pausedSpreadMs), 15_000);
      check(ctx, 'seek to 60 s while paused lands everywhere', seeked.ok, `${seeked.elapsedMs} ms; ${describe(seeked.snapshot)}`);
      await sleep(5000);
      const resumeAt = Date.now();
      await group.driver.unpause();
      const playing = await waitUntil(group.all, allPlaying, 15_000);
      check(ctx, 'group resumes after a paused seek', playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`, KNOWN_ISSUES.pausedSeekBuffering);
      if (playing.ok) await measureDrift(ctx, group.all, 'after-paused-seek', resumeAt);
    },
  },
  {
    name: 'leave-rejoin',
    title: 'A Web participant leaves and rejoins a playing group, then follows it',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(5000);
      const web = group.webs[1];
      check(ctx, 'leave accepted', (await web.leave()) === 204, 'HTTP 204 expected');
      await sleep(3000);
      const status = await web.join(group.groupId);
      check(ctx, 'rejoin accepted', status === 204, `HTTP ${status}`);
      const rejoinAt = Date.now();
      const playing = await waitUntil(group.all, allPlaying, 30_000);
      check(ctx, 'rejoined participant plays with the group', playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`, KNOWN_ISSUES.rejoinIgnoresQueue);
      if (playing.ok) await measureDrift(ctx, group.all, 'after-rejoin', rejoinAt);
    },
  },
  {
    name: 'legacy-participant',
    title: 'A client without SupportsSyncPlayPlaybackRate: 409 on join to 1.5x and on rate change, no state change',
    async run(ctx) {
      const group = await startGroup(ctx, { webCount: 1 });
      await changeRate(ctx, group, 1.5);
      const legacyUser = ctx.users[3];
      const legacy = new ProtocolParticipant(ctx.baseUrl, legacyUser, { label: `${legacyUser.name}-legacy`, supportsRate: false, log: ctx.log });
      await legacy.start();
      ctx.cleanup.push(() => legacy.stop());
      let res = await legacy.join(group.groupId);
      check(ctx, 'legacy join to 1.5x group is rejected with 409', res.status === 409, `HTTP ${res.status}`);
      await sleep(1000);
      let snap = await snapshot(group.all);
      check(ctx, 'group keeps playing at 1.5x after the rejected join', snap.samples.every((s) => s.playing && Math.abs(s.rate - 1.5) < 0.15), describe(snap));

      await changeRate(ctx, group, 1.0);
      res = await legacy.join(group.groupId);
      check(ctx, 'legacy join at 1x is accepted', res.status === 204, `HTTP ${res.status}`);
      await sleep(2000);
      res = await group.driver.setPlaybackRate(1.25);
      check(ctx, 'rate change with a legacy member is rejected with 409', res.status === 409, `HTTP ${res.status}`);
      await sleep(2000);
      snap = await snapshot(group.all);
      check(ctx, 'group still at 1x after the rejected rate change', snap.samples.every((s) => s.playing && Math.abs(s.rate - 1.0) < 0.15), describe(snap));
    },
  },
];
