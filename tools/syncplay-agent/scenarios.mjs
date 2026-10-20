// SyncPlay scenarios for our client against a live server. Participants:
// the harness's protocol driver (the exact reference, it also drives the
// group), Jellyfin Web in headless Chromium, and our client (NativeParticipant).
// Thresholds are the stage 1 rows «Дрейф, автоматически», «Обрыв сети» and
// «Обычный сервер» of docs/architecture (decision-and-plan.md).
import { ProtocolParticipant } from '../syncplay-harness/src/protocolClient.mjs';
import { WebParticipant } from '../syncplay-harness/src/webParticipant.mjs';
import { snapshot, waitUntil, allPlaying, allPaused, round } from '../syncplay-harness/src/measure.mjs';
import { sleep } from '../syncplay-harness/src/jellyfin.mjs';
import { NativeParticipant } from './nativeParticipant.mjs';

export const THRESHOLDS = {
  driftMs: 400,
  driftCheckpointsS: [10, 15, 20],
  pausedSpreadMs: 400,
  // A command (pause, seek, rate) reaches everyone within this time.
  propagationMs: 3000,
  // After the network comes back our client is in the group and in sync within this time.
  reconnectMs: 15_000,
  // Long run: sampled every second, p95 of the spread.
  longP95Ms: 400,
};

function check(ctx, name, passed, detail) {
  ctx.checks.push({ name, passed: Boolean(passed), detail });
  ctx.log(`${passed ? '  ok ' : '  FAIL'} ${name}: ${detail}`);
}

function describe(snap) {
  if (!snap) return 'no sample';
  return snap.samples.map((s) => `${s.label}=${s.projectedMs == null ? 'n/a' : (s.projectedMs / 1000).toFixed(3)}s${s.playing ? `@${round(s.rate, 3)}x` : ' paused'}`).join(', ');
}

function nativeOf(group) { return group.native; }

/**
 * A group of the protocol driver, `webCount` Web clients and our client.
 * With `nativeLate`, our client joins only after the group plays (a cold join).
 */
async function startGroup(ctx, { webCount = ctx.webCount, nativeLate = false, speedErrorPpm = ctx.speedErrorPpm } = {}) {
  const [driverUser, nativeUser, ...webUsers] = ctx.users;
  const driver = new ProtocolParticipant(ctx.baseUrl, driverUser, { log: ctx.log });
  await driver.start();
  ctx.cleanup.push(() => driver.stop());
  const groupId = await driver.createGroup(`native-${ctx.name}-${Date.now()}`);

  const native = new NativeParticipant(ctx.baseUrl, nativeUser, { agent: ctx.agent, speedErrorPpm, log: ctx.log });
  await native.start();
  ctx.cleanup.push(() => native.stop());
  ctx.natives.push(native);

  const webs = [];
  for (const user of webUsers.slice(0, webCount)) {
    const web = new WebParticipant(ctx.browser, ctx.baseUrl, user, { log: ctx.log });
    await web.start();
    ctx.cleanup.push(() => web.stop());
    const status = await web.join(groupId);
    if (status >= 300) throw new Error(`${web.label} could not join: HTTP ${status}`);
    webs.push(web);
  }
  if (!nativeLate) {
    const status = await native.join(groupId);
    check(ctx, 'our client joins the group', status < 300, `HTTP ${status}`);
  }
  const others = [driver, ...webs];
  const all = nativeLate ? others : [...others, native];
  await driver.setNewQueue([ctx.server.itemId], 0);
  const loaded = await waitUntil(all, (s) => s.samples.every((x) => x.positionMs != null), 60_000, 250);
  if (!loaded.ok) throw new Error(`Media did not load everywhere: ${describe(loaded.snapshot)}`);
  const unpauseAt = Date.now();
  await driver.unpause();
  const playing = await waitUntil(all, allPlaying, 30_000);
  check(ctx, 'group starts playing', playing.ok, `${playing.elapsedMs} ms after Unpause; ${describe(playing.snapshot)}`);
  ctx.metrics.startLatencyMs = Date.now() - unpauseAt;

  const group = { driver, webs, native, groupId, all: [...others, native] };
  if (nativeLate) {
    await sleep(5000);
    const joinAt = Date.now();
    const status = await native.join(groupId);
    check(ctx, 'our client joins the playing group', status < 300, `HTTP ${status}`);
    const synced = await waitUntil(group.all, (s) => allPlaying(s) && s.spreadMs < THRESHOLDS.driftMs, 30_000);
    check(ctx, 'cold join: in sync with the playing group', synced.ok, `${synced.elapsedMs} ms after Join; ${describe(synced.snapshot)}`);
    ctx.metrics.coldJoinMs = synced.elapsedMs;
    return { ...group, since: joinAt };
  }
  return { ...group, since: unpauseAt };
}

async function measureDrift(ctx, group, label, sinceMs) {
  const results = [];
  for (const checkpoint of THRESHOLDS.driftCheckpointsS) {
    const wait = sinceMs + checkpoint * 1000 - Date.now();
    if (wait > 0) await sleep(wait);
    const snap = await snapshot(group.all);
    const ok = snap.spreadMs != null && snap.spreadMs < THRESHOLDS.driftMs && snap.samples.every((s) => s.playing);
    const nativeSample = snap.samples.find((s) => s.label === group.native.label);
    const reference = snap.samples.find((s) => s.label === group.driver.label);
    const nativeError = nativeSample && reference ? round(nativeSample.projectedMs - reference.projectedMs) : null;
    results.push({ checkpointS: checkpoint, spreadMs: round(snap.spreadMs), nativeErrorMs: nativeError });
    check(ctx, `${label}: drift after ${checkpoint} s`, ok, `spread ${round(snap.spreadMs)} ms, our client ${nativeError} ms from the reference (limit ${THRESHOLDS.driftMs}); ${describe(snap)}`);
  }
  ctx.metrics[`drift_${label}`] = results;
  return results;
}

async function changeRate(ctx, group, rate, requester = 'native') {
  if (requester === 'native') group.native.setRate(rate);
  else await group.driver.setPlaybackRate(rate);
  await sleep(300);
  const playing = await waitUntil(group.all, (s) => allPlaying(s) && s.samples.every((x) => Math.abs(x.rate - rate) < 0.15 * rate), 20_000);
  check(ctx, `everyone plays at ${rate}x (requested by ${requester === 'native' ? 'our client' : 'the driver'})`, playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`);
  return Date.now();
}

async function nativeStats(ctx, group) {
  const s = await group.native.sample();
  ctx.metrics.nativeCorrections = { speed: s.corrections, skip: s.skips };
  return s;
}

export const scenarios = [
  {
    name: 'native-drift',
    title: 'Our client in a mixed group: drift at 1x, 1.25x and 1.5x at 10/15/20 s, rate changes from our client',
    forkOnly: true,
    async run(ctx) {
      const group = await startGroup(ctx);
      await measureDrift(ctx, group, '1x', group.since);
      for (const rate of [1.25, 1.5]) {
        const since = await changeRate(ctx, group, rate);
        await measureDrift(ctx, group, `${rate}x`, since);
      }
      await changeRate(ctx, group, 1.0, 'driver');
      await nativeStats(ctx, group);
    },
  },
  {
    name: 'native-cold-join',
    title: 'Our client joins a group that is already playing at 1.5x and stays in sync',
    forkOnly: true,
    async run(ctx) {
      const group = await startGroup(ctx, { nativeLate: true });
      // The group moves to 1.5x before the cold join is measured further.
      const since = await changeRate(ctx, group, 1.5, 'driver');
      await measureDrift(ctx, group, 'cold-1.5x', since);
      await nativeStats(ctx, group);
    },
  },
  {
    name: 'native-controls',
    title: 'Pause, seek while paused and resume requested by our client; everyone follows',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(4000);
      group.native.pause();
      const paused = await waitUntil(group.all, allPaused, THRESHOLDS.propagationMs);
      check(ctx, 'pause from our client reaches everyone', paused.ok, `${paused.elapsedMs} ms; ${describe(paused.snapshot)}`);
      await sleep(500);
      let snap = await snapshot(group.all);
      check(ctx, 'paused positions agree', snap.spreadMs != null && snap.spreadMs < THRESHOLDS.pausedSpreadMs, `spread ${round(snap.spreadMs)} ms; ${describe(snap)}`);
      group.native.seek(90_000);
      const seeked = await waitUntil(group.all, (s) => allPaused(s) && s.samples.every((x) => Math.abs(x.positionMs - 90_000) < THRESHOLDS.pausedSpreadMs), 15_000);
      check(ctx, 'seek to 90 s from our client lands everywhere, paused', seeked.ok, `${seeked.elapsedMs} ms; ${describe(seeked.snapshot)}`);
      // Web 12.2 cannot resume after a paused seek (harness known issue 2), so
      // resuming is checked without Web participants in the group's way: the
      // Web clients leave first when present.
      for (const web of group.webs) await web.leave();
      group.all = [group.driver, group.native];
      await sleep(1000);
      const resumeAt = Date.now();
      group.native.play();
      const playing = await waitUntil(group.all, allPlaying, THRESHOLDS.propagationMs * 2);
      check(ctx, 'resume from our client after a paused seek', playing.ok, `${playing.elapsedMs} ms; ${describe(playing.snapshot)}`);
      if (playing.ok) await measureDrift(ctx, group, 'after-seek', resumeAt);
      // Quitting the app leaves the group, so the others do not wait for us.
      const left = group.driver.waitFor((e) => e.data?.Type === 'UserLeft', 5_000, 'UserLeft').then(() => true, () => false);
      const quitAt = Date.now();
      await group.native.stop();
      check(ctx, 'our client leaves the group when it quits', await left, `${Date.now() - quitAt} ms`);
    },
  },
  {
    name: 'native-network-drop',
    title: 'Our client loses the network for 10 s while the group seeks; it comes back on its own',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(5000);
      const drop = group.native.dropConnection(10_000);
      await sleep(3000);
      // The group moves on while our client is away.
      await group.driver.seek(120_000);
      await sleep(1500);
      await drop;
      const back = Date.now();
      const joined = await group.native.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', THRESHOLDS.reconnectMs, 'GroupJoined after reconnect').then(() => true, () => false);
      check(ctx, 'back in the group without user action', joined, `${Date.now() - back} ms after the network returned`);
      const synced = await waitUntil(group.all, (s) => allPlaying(s) && s.spreadMs < THRESHOLDS.driftMs, THRESHOLDS.reconnectMs);
      check(ctx, 'in sync after the network returned', synced.ok, `${synced.elapsedMs} ms; ${describe(synced.snapshot)}`);
      ctx.metrics.reconnectSyncMs = Date.now() - back;
      if (synced.ok) await measureDrift(ctx, group, 'after-reconnect', Date.now());
      const s = await nativeStats(ctx, group);
      check(ctx, 'our client stays a single group member', s.inGroup, `inGroup=${s.inGroup}`);
    },
  },
  {
    name: 'native-sleep',
    title: 'The computer sleeps for 20 s while the group seeks; our client wakes to dead connections and comes back on its own',
    async run(ctx) {
      const group = await startGroup(ctx);
      await sleep(5000);
      const nap = group.native.sleepFor(20_000);
      await sleep(3000);
      await group.driver.seek(120_000);
      await sleep(15_000);
      const noticed = group.native.waitFor((e) => e.type === 'resumed', 25_000, 'wake').then(() => Date.now(), () => null);
      const joined = group.native.waitFor((e) => e.type === 'SyncPlayGroupUpdate' && e.data?.Type === 'GroupJoined', 25_000 + THRESHOLDS.reconnectMs, 'GroupJoined after waking').then(() => Date.now(), () => null);
      await nap;
      const woke = Date.now();
      const noticedAt = await noticed;
      check(ctx, 'our client notices it slept', noticedAt != null, noticedAt ? `${noticedAt - woke} ms after waking` : 'no wake notice');
      const joinedAt = await joined;
      check(ctx, 'back in the group without user action', joinedAt != null, joinedAt ? `${joinedAt - woke} ms after waking` : 'not back in the group');
      const synced = await waitUntil(group.all, (s) => allPlaying(s) && s.spreadMs < THRESHOLDS.driftMs, THRESHOLDS.reconnectMs);
      check(ctx, 'in sync after waking', synced.ok, `${synced.elapsedMs} ms; ${describe(synced.snapshot)}`);
      ctx.metrics.wakeSyncMs = Date.now() - woke;
      if (synced.ok) await measureDrift(ctx, group, 'after-sleep', Date.now());
      const s = await nativeStats(ctx, group);
      check(ctx, 'our client stays a single group member', s.inGroup, `inGroup=${s.inGroup}`);
    },
  },
  {
    name: 'stock-server',
    title: 'Stock Jellyfin 12.2 without the rate extension: SyncPlay at 1x works, a rate change is refused',
    stockOnly: true,
    async run(ctx) {
      const group = await startGroup(ctx);
      await measureDrift(ctx, group, 'stock-1x', group.since);
      const refused = group.native.waitFor((e) => e.type === 'notice' && e.data.kind === 'groupRateRejected', 10_000, 'rate refusal').then((e) => e.data, () => null);
      group.native.setRate(1.5);
      const notice = await refused;
      check(ctx, 'rate change is refused and reported to the app', notice != null, notice ? `HTTP ${notice.status}` : 'no refusal notice');
      await sleep(2000);
      const snap = await snapshot(group.all);
      check(ctx, 'everyone keeps playing at 1x', snap.samples.every((s) => s.playing && Math.abs(s.rate - 1) < 0.15), describe(snap));
      await measureDrift(ctx, group, 'stock-1x-after-refusal', Date.now());
      await nativeStats(ctx, group);
    },
  },
  {
    name: 'native-long',
    title: 'Long run: 1x, 1.25x and 1.5x in turn, spread sampled every second, p95',
    longOnly: true,
    async run(ctx) {
      const group = await startGroup(ctx);
      const total = ctx.longMinutes * 60_000;
      const rates = ctx.serverKind === 'stock' ? [1] : [1, 1.25, 1.5];
      const spreads = [];
      const started = Date.now();
      let segment = 0;
      while (Date.now() - started < total) {
        const rate = rates[segment % rates.length];
        if (segment > 0 && rates.length > 1) await changeRate(ctx, group, rate, segment % 2 ? 'native' : 'driver');
        const segmentEnd = Math.min(started + total, Date.now() + total / 6);
        // Restart near the beginning before the clip runs out.
        while (Date.now() < segmentEnd) {
          const snap = await snapshot(group.all);
          if (snap.samples.every((s) => s.playing) && snap.spreadMs != null) spreads.push(snap.spreadMs);
          const ref = snap.samples.find((s) => s.label === group.driver.label);
          if (ref && ref.projectedMs > ctx.server.runTimeTicks / 10_000 - 60_000) {
            await group.driver.seek(0);
            await sleep(2000);
          }
          await sleep(1000);
        }
        segment++;
      }
      spreads.sort((a, b) => a - b);
      const p95 = spreads[Math.floor(spreads.length * 0.95)] ?? null;
      ctx.metrics.long = { minutes: ctx.longMinutes, samples: spreads.length, p50Ms: round(spreads[Math.floor(spreads.length / 2)]), p95Ms: round(p95), maxMs: round(spreads.at(-1)) };
      check(ctx, `p95 spread over ${ctx.longMinutes} min`, p95 != null && p95 < THRESHOLDS.longP95Ms, `p95 ${round(p95)} ms, max ${round(spreads.at(-1))} ms over ${spreads.length} samples`);
      await nativeStats(ctx, group);
    },
  },
];
