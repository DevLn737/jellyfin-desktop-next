#!/usr/bin/env node
// Checkpoint 2's mixed group with real playback: our client as the app runs
// it (jfd_syncplay_app_agent: headless mpv, the player's model and the
// watch-together model) starts a group from an item page with Watch
// together, Jellyfin Web joins in headless Chromium, our client switches to
// the external audio file while Web keeps its own audio, and our client's
// Skip button on the intro moves the whole group. Runs on the extras stand
// (scripts/test-server/extras-server.sh: an external audio file, intro
// 2-12 s). Writes mixed-group.json and mixed-group.md; exit code 1 when a
// check fails.
//
//   node tools/syncplay-agent/mixedGroup.mjs --agent build/tools/syncplay-agent/app/jfd_syncplay_app_agent \
//     [--url $JFD_EXTRAS_SERVER_URL] [--user jfd-test] [--password ...] [--out out/mixed]
import { mkdir, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { parseArgs } from 'node:util';
import { JellyfinHttp, sleep, waitForHealth } from '../syncplay-harness/src/jellyfin.mjs';
import { snapshot, waitUntil, allPlaying, round } from '../syncplay-harness/src/measure.mjs';
import { WebParticipant } from '../syncplay-harness/src/webParticipant.mjs';
import { NativeParticipant } from './nativeParticipant.mjs';

const { values: args } = parseArgs({
  options: {
    agent: { type: 'string' },
    url: { type: 'string', default: process.env.JFD_EXTRAS_SERVER_URL ?? 'http://127.0.0.1:18097' },
    user: { type: 'string', default: process.env.JFD_EXTRAS_USER ?? 'jfd-test' },
    password: { type: 'string', default: process.env.JFD_EXTRAS_PASSWORD ?? 'jfd-test-password' },
    out: { type: 'string', default: 'out/mixed' },
  },
});
if (!args.agent) throw new Error('--agent is required');

// The stage 1 drift row, and how long a command may take to reach everyone.
const DRIFT_MS = 400;
const PROPAGATION_MS = 3000;
const WEB_USER = { name: 'jfd-web', password: 'jfd-web-password' };

const log = (msg) => console.log(`${new Date().toISOString().slice(11, 23)} ${msg}`);
const checks = [];
const metrics = {};
function check(name, passed, detail) {
  checks.push({ name, passed: Boolean(passed), detail });
  log(`${passed ? '  ok ' : '  FAIL'} ${name}: ${detail}`);
}
const describe = (snap) =>
  !snap ? 'no sample' : snap.samples.map((s) => `${s.label}=${s.projectedMs == null ? 'n/a' : (s.projectedMs / 1000).toFixed(3)}s${s.playing ? `@${round(s.rate, 3)}x` : ' paused'}`).join(', ');

// Spread at a few instants, `everyMs` apart.
async function driftChecks(all, label, count = 3, everyMs = 3000) {
  const spreads = [];
  for (let i = 0; i < count; i++) {
    await sleep(everyMs);
    const snap = await snapshot(all);
    spreads.push(snap.spreadMs);
    check(`${label}: spread ${i + 1}/${count}`, snap.spreadMs != null && snap.spreadMs < DRIFT_MS && allPlaying(snap),
      `${round(snap.spreadMs)} ms (limit ${DRIFT_MS}); ${describe(snap)}`);
  }
  metrics[label] = Math.max(...spreads.map((v) => v ?? Infinity));
}

await mkdir(args.out, { recursive: true });
await waitForHealth(args.url);
const admin = new JellyfinHttp(args.url, { device: 'mixed-group-setup' });
await admin.login(args.user, args.password);
const users = await admin.get('Users');
if (!users.some((u) => u.Name === WEB_USER.name)) {
  log(`Creating user ${WEB_USER.name}`);
  await admin.post('Users/New', { Name: WEB_USER.name, Password: WEB_USER.password });
}
// Web's own subtitle choice comes from its user's preference: English,
// always shown. (Choosing in Web's player menu would need its UI.)
const webUser = (await admin.get('Users')).find((u) => u.Name === WEB_USER.name);
await admin.post(`Users/Configuration?userId=${webUser.Id}`,
  { ...webUser.Configuration, SubtitleLanguagePreference: 'eng', SubtitleMode: 'Always' });
const movies = await admin.get('Items?Recursive=true&IncludeItemTypes=Movie');
const movie = movies.Items[0];
log(`Item: ${movie.Name} (${movie.Id})`);

const require = createRequire(new URL('../syncplay-harness/package.json', import.meta.url));
const { chromium } = require('playwright');
const browser = await chromium.launch({ headless: true, args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'] });
const native = new NativeParticipant(args.url, { name: args.user, password: args.password }, { agent: args.agent, label: 'our-client', log });
const web = new WebParticipant(browser, args.url, WEB_USER, { label: 'web', log });
const all = [native, web];
let error = null;
try {
  await native.start();
  await web.start();

  // 1. Watch together on the item page: a group named after the item that
  //    starts playing it.
  const groupId = await native.watchTogether(movie.Id, movie.Name);
  check('our client starts a group from the item page', Boolean(groupId), `group ${groupId}`);
  const started = await waitUntil([native], allPlaying, 60_000, 250);
  check('the group plays the item on our client', started.ok, `${started.elapsedMs} ms; ${describe(started.snapshot)}`);

  // 2. Web joins the playing group and catches up.
  const joinAt = Date.now();
  const status = await web.join(groupId);
  check('Jellyfin Web joins', status < 300, `HTTP ${status}`);
  const caughtUp = await waitUntil(all, (s) => allPlaying(s) && s.spreadMs != null && s.spreadMs < DRIFT_MS, 30_000, 250);
  metrics.webJoinMs = Date.now() - joinAt;
  check('Web is in step with our client', caughtUp.ok, `${metrics.webJoinMs} ms after Join; ${describe(caughtUp.snapshot)}`);
  await driftChecks(all, 'mixed group');

  // 3. Each member on its own audio: our client plays the external file
  //    and moves to the track Web is not on; Web keeps its own, and the
  //    group plays on in step.
  const menus = await native.tracks();
  const external = menus.audio.find((t) => t.isExternal);
  check('our audio menu lists the external file', Boolean(external), JSON.stringify(menus.audio.map((t) => [t.index, t.title ?? t.language, t.isExternal])));
  const webAudio = async () => (await admin.get('Sessions')).find((s) => s.UserName === WEB_USER.name && s.NowPlayingItem)?.PlayState?.AudioStreamIndex;
  const ourAudio = async () => (await admin.get('Sessions')).find((s) => s.DeviceId === `jfd-agent-${native.label}`)?.PlayState?.AudioStreamIndex;
  const audioOf = async (index) => {
    if (menus.audioIndex !== index) {
      native.selectAudio(index);
      const switched = await waitUntil([native], (s) => s.samples[0].audioIndex === index && s.samples[0].playing, 30_000, 250);
      if (!switched.ok) return null;
    }
    menus.audioIndex = index;
    return native.tracks();
  };
  let onExternal = await audioOf(external.index);
  for (const deadline = Date.now() + 5000; onExternal && !onExternal.mpvAudioExternal && Date.now() < deadline; onExternal = await native.tracks()) await sleep(250);
  const method = (await admin.get('Sessions')).find((x) => x.DeviceId === `jfd-agent-${native.label}`)?.PlayState?.PlayMethod;
  metrics.audioPlayback = method === 'Transcode' ? 'mixed into a server transcode' : `a file beside the video (${method})`;
  // Played directly, mpv loads the file beside the video; transcoded, the
  // server mixes it in and mpv sees one track.
  check('our client plays the external audio file', onExternal && (await ourAudio()) === external.index &&
    (method === 'Transcode' || (onExternal.mpvAudioExternal && onExternal.mpvAudioLang === 'rus')),
    `mpv external ${onExternal?.mpvAudioExternal}, language ${onExternal?.mpvAudioLang}; played as ${metrics.audioPlayback}`);
  const webBefore = await webAudio();
  const other = menus.audio.find((t) => t.index !== webBefore) ?? menus.audio.find((t) => t.index !== external.index);
  const switchedTo = await audioOf(other.index);
  // The server hears of it with the next progress report.
  let serverShows = await ourAudio();
  for (const deadline = Date.now() + 15_000; serverShows !== other.index && Date.now() < deadline; serverShows = await ourAudio()) await sleep(500);
  check('our client switches to a track Web is not on', switchedTo?.audioIndex === other.index && serverShows === other.index,
    `ours ${other.index} (${other.title}), the player shows ${switchedTo?.audioIndex}, the server ${serverShows}`);
  const webAfter = await webAudio();
  check('Web keeps its own audio', webAfter === webBefore && webAfter !== other.index, `Web ${webBefore} -> ${webAfter}, ours ${other.index}`);
  const resynced = await waitUntil(all, (s) => allPlaying(s) && s.spreadMs != null && s.spreadMs < DRIFT_MS, 20_000, 250);
  check('the group is in step after our switch', resynced.ok, `${resynced.elapsedMs} ms; ${describe(resynced.snapshot)}`);
  await driftChecks(all, 'own audio', 2);

  // 3b. Each member on its own external subtitles (when the stand has the
  //     Russian .srt and English .ass): Web shows the English one from its
  //     user's preference, our client picks the Russian one.
  const rusSub = menus.subtitle.find((t) => t.isExternal && t.language === 'rus');
  const engSub = menus.subtitle.find((t) => t.isExternal && t.language === 'eng');
  if (rusSub && engSub) {
    const sessionOf = async (match) => (await admin.get('Sessions')).find(match)?.PlayState?.SubtitleStreamIndex;
    const webSub = () => sessionOf((x) => x.UserName === WEB_USER.name && x.NowPlayingItem);
    const ourSub = () => sessionOf((x) => x.DeviceId === `jfd-agent-${native.label}`);
    const until = async (read, want, ms = 15_000) => {
      let value = await read();
      for (const deadline = Date.now() + ms; value !== want && Date.now() < deadline; value = await read()) await sleep(500);
      return value;
    };
    const webBeforeSub = await until(webSub, engSub.index);
    check('Web shows its English .ass', webBeforeSub === engSub.index, `Web subtitle ${webBeforeSub}, English ${engSub.index}`);
    native.selectSubtitle(rusSub.index);
    const picked = await waitUntil([native], (x) => x.samples[0].subtitleIndex === rusSub.index && x.samples[0].playing, 30_000, 250);
    const ours = await until(ourSub, rusSub.index);
    let shown = await native.tracks();
    for (const deadline = Date.now() + 5000; shown.mpvSubtitleLang !== 'rus' && Date.now() < deadline; shown = await native.tracks()) await sleep(250);
    check('our client shows its Russian .srt', picked.ok && ours === rusSub.index && shown.mpvSubtitleLang === 'rus',
      `ours ${ours}, Russian ${rusSub.index}, mpv shows ${shown.mpvSubtitleLang || 'none'}`);
    const webAfterSub = await webSub();
    check('Web keeps its English subtitles', webAfterSub === engSub.index, `Web ${webBeforeSub} -> ${webAfterSub}`);
    const inStep = await waitUntil(all, (x) => allPlaying(x) && x.spreadMs != null && x.spreadMs < DRIFT_MS, 20_000, 250);
    check('the group is in step with our subtitles on', inStep.ok, `${inStep.elapsedMs} ms; ${describe(inStep.snapshot)}`);
    await driftChecks(all, 'own subtitles', 2);
  } else {
    log('The stand has no Russian and English external subtitles: subtitle step skipped');
  }

  // 4. The intro: back to it for everyone, then our Skip button moves the
  //    whole group past it (intro 2-12 s on the stand).
  native.seek(3000);
  const inIntro = await waitUntil(all, (s) => allPlaying(s) && s.samples.every((x) => x.projectedMs < 10_000), 20_000, 250);
  check('a seek into the intro reaches everyone', inIntro.ok, describe(inIntro.snapshot));
  const segment = await waitUntil([native], (s) => s.samples[0].segment === 'Intro', 10_000, 100);
  check('our player shows the intro Skip button', segment.ok, `segment "${segment.snapshot?.samples[0].segment}"`);
  const skipAt = Date.now();
  native.skipSegment();
  const skipped = await waitUntil(all, (s) => allPlaying(s) && s.samples.every((x) => x.projectedMs >= 11_900), PROPAGATION_MS + 5000, 100);
  metrics.skipMs = Date.now() - skipAt;
  check('Skip moves everyone past the intro', skipped.ok && metrics.skipMs < PROPAGATION_MS + 5000, `${metrics.skipMs} ms; ${describe(skipped.snapshot)}`);
  const afterSkip = await waitUntil(all, (s) => allPlaying(s) && s.spreadMs != null && s.spreadMs < DRIFT_MS, 15_000, 250);
  check('in step after the skip', afterSkip.ok, describe(afterSkip.snapshot));
  await driftChecks(all, 'after skip', 2);
} catch (e) {
  error = e.stack ?? String(e);
  log(`ERROR ${e.message}`);
}
// What our client heard and sent, for reading a failure next to the server log.
const journal = await Promise.race([native.journal(), sleep(3_000).then(() => [])]).catch(() => []);
await writeFile(`${args.out}/session-journal.txt`, `${journal.join('\n')}\n`);
await native.stop().catch(() => {});
await web.stop().catch(() => {});
await browser.close();

const passed = !error && checks.length > 0 && checks.every((c) => c.passed);
const report = { generatedAt: new Date().toISOString(), url: args.url, item: movie.Name, passed, error, checks, metrics };
await writeFile(`${args.out}/mixed-group.json`, JSON.stringify(report, null, 2));
const lines = [
  '# Mixed group with real playback: our client (mpv) and Jellyfin Web',
  '',
  `${passed ? '✅ All checks passed' : '❌ Failures'} · ${movie.Name} on the extras stand · ${report.generatedAt}`,
  '',
  `Max spread: mixed group ${round(metrics['mixed group'])} ms, own audio ${round(metrics['own audio'])} ms, own subtitles ${round(metrics['own subtitles'])} ms, after skip ${round(metrics['after skip'])} ms (limit ${DRIFT_MS}). Web joined in ${metrics.webJoinMs} ms; Skip reached everyone in ${metrics.skipMs} ms. Our external audio played as ${metrics.audioPlayback ?? 'n/a'}.`,
  '',
];
if (error) lines.push('```', error.split('\n').slice(0, 6).join('\n'), '```', '');
for (const c of checks) lines.push(`- ${c.passed ? '✅' : '❌'} ${c.name}: ${c.detail}`);
await writeFile(`${args.out}/mixed-group.md`, `${lines.join('\n')}\n`);
log(`Report: ${args.out}/mixed-group.md (${passed ? 'all passed' : 'FAILURES'})`);
process.exit(passed ? 0 : 1);
