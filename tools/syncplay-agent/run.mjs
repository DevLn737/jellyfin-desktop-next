#!/usr/bin/env node
// Runs our client's SyncPlay scenarios against a bootstrapped test server and
// writes drift-report.json + drift-report.md. Exit code 1 when a check fails.
//
//   node tools/syncplay-agent/run.mjs --agent build/tools/syncplay-agent/jfd_syncplay_agent \
//     [--server fork|stock] [--url http://localhost:8096] [--web 1] [--only a,b] [--long 30] [--out dir]
import { mkdir, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { parseArgs } from 'node:util';
import { bootstrap, USERS } from '../syncplay-harness/src/bootstrap.mjs';
import { scenarios, THRESHOLDS } from './scenarios.mjs';

const { values: args } = parseArgs({
  options: {
    agent: { type: 'string' },
    server: { type: 'string', default: 'fork' },
    url: { type: 'string', default: process.env.JELLYFIN_URL ?? 'http://localhost:8096' },
    web: { type: 'string', default: '1' },
    'speed-error-ppm': { type: 'string', default: '3000' },
    only: { type: 'string' },
    long: { type: 'string' },
    out: { type: 'string', default: 'out' },
  },
});
if (!args.agent) throw new Error('--agent is required');

const log = (msg) => console.log(`${new Date().toISOString().slice(11, 23)} ${msg}`);
await mkdir(args.out, { recursive: true });
const server = await bootstrap(args.url, { log });
const webCount = Number(args.web);
const longMinutes = args.long ? Number(args.long) : 0;
let browser = null;
if (webCount > 0) {
  // Playwright is installed with the harness (npm ci in tools/syncplay-harness).
  const require = createRequire(new URL('../syncplay-harness/package.json', import.meta.url));
  const { chromium } = require('playwright');
  browser = await chromium.launch({ headless: true, args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'] });
}

const selected = scenarios.filter((s) => {
  if (args.only) return args.only.split(',').includes(s.name);
  if (s.longOnly) return longMinutes > 0;
  if (s.stockOnly) return args.server === 'stock';
  if (s.forkOnly) return args.server === 'fork';
  return true;
});

const results = [];
for (const scenario of selected) {
  log(`=== ${scenario.name}: ${scenario.title}`);
  const ctx = {
    name: scenario.name, baseUrl: args.url, browser, server, users: USERS, log, agent: args.agent,
    webCount, longMinutes, serverKind: args.server, speedErrorPpm: Number(args['speed-error-ppm']),
    checks: [], metrics: {}, cleanup: [], natives: [],
  };
  const started = Date.now();
  let error = null;
  try {
    await scenario.run(ctx);
  } catch (e) {
    error = e.stack ?? String(e);
    log(`ERROR ${e.message}`);
  }
  for (const fn of ctx.cleanup.reverse()) await fn().catch(() => {});
  await writeRecordings(scenario, ctx, started);
  const passed = !error && ctx.checks.length > 0 && ctx.checks.every((c) => c.passed);
  results.push({ name: scenario.name, title: scenario.title, passed, error, durationMs: Date.now() - started, checks: ctx.checks, metrics: ctx.metrics });
  log(`${passed ? 'PASS' : 'FAIL'} ${scenario.name}`);
}
await browser?.close();

const report = {
  generatedAt: new Date().toISOString(),
  server: { kind: args.server, url: args.url, version: server.serverVersion },
  webParticipants: webCount,
  speedErrorPpm: Number(args['speed-error-ppm']),
  thresholds: THRESHOLDS,
  passed: results.every((r) => r.passed),
  scenarios: results,
};
await writeFile(`${args.out}/drift-report.json`, JSON.stringify(report, null, 2));
await writeFile(`${args.out}/drift-report.md`, renderMarkdown(report));
log(`Report: ${args.out}/drift-report.md (${report.passed ? 'all passed' : 'FAILURES'})`);
process.exit(report.passed ? 0 : 1);

// What our client received from the server, in order: the stand's message
// sequences for the simulator and the protocol tests (tests/syncplay/fixtures).
async function writeRecordings(scenario, ctx, started) {
  if (ctx.natives.length === 0) return;
  await mkdir(`${args.out}/recordings`, { recursive: true });
  const recording = {
    scenario: scenario.name,
    title: scenario.title,
    server: { kind: args.server, version: server.serverVersion },
    participants: ctx.natives.map((native) => ({
      label: native.label,
      events: native.events.flatMap((e) => {
        const ms = e.at - started;
        if (e.type === 'SyncPlayCommand' || e.type === 'SyncPlayGroupUpdate') return [{ ms, message: { MessageType: e.type, Data: e.data } }];
        if (e.type === 'request') return [{ ms, request: e.data }];
        if (e.type === 'ready' || e.type === 'disconnected' || e.type === 'resumed') return [{ ms, connection: e.type }];
        return [];
      }),
    })),
  };
  await writeFile(`${args.out}/recordings/${scenario.name}.json`, `${JSON.stringify(recording, null, 1)}\n`);
}

function renderMarkdown(r) {
  const lines = [
    `# SyncPlay drift report: our client, ${r.server.kind === 'stock' ? 'stock' : 'forked'} Jellyfin ${r.server.version}`,
    '',
    `${r.passed ? '✅ All checks passed' : '❌ Failures'} · ${r.webParticipants} Jellyfin Web participant(s) · our client's clock off by ${r.speedErrorPpm} ppm · ${r.generatedAt}`,
    '',
    '| Scenario | Result | Key numbers |',
    '| --- | --- | --- |',
  ];
  for (const s of r.scenarios) {
    lines.push(`| ${s.name} | ${s.passed ? '✅' : '❌'} ${s.checks.filter((c) => c.passed).length}/${s.checks.length}${s.error ? ' (error)' : ''} | ${keyNumbers(s.metrics)} |`);
  }
  for (const s of r.scenarios) {
    lines.push('', `<details${s.passed ? '' : ' open'}><summary>${s.name}: ${s.title}</summary>`, '');
    if (s.error) lines.push('```', s.error.split('\n').slice(0, 6).join('\n'), '```', '');
    for (const c of s.checks) lines.push(`- ${c.passed ? '✅' : '❌'} ${c.name}: ${c.detail}`);
    lines.push('', '</details>');
  }
  return `${lines.join('\n')}\n`;
}

function keyNumbers(metrics) {
  const parts = [];
  for (const [key, value] of Object.entries(metrics)) {
    if (key.startsWith('drift_')) parts.push(`${key.slice(6)}: max spread ${Math.max(...value.map((v) => v.spreadMs ?? Infinity))} ms`);
    else if (key === 'nativeCorrections') parts.push(`corrections ${value.speed} speed / ${value.skip} skip`);
    else if (key === 'long') parts.push(`${value.minutes} min p95 ${value.p95Ms} ms`);
    else if (typeof value === 'number') parts.push(`${key} ${value} ms`);
  }
  return parts.join('; ') || '—';
}
