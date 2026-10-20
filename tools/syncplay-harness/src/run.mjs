#!/usr/bin/env node
// Runs the SyncPlay scenarios against a bootstrapped test server and writes
// report.json + report.md. Exit code 1 when any check fails.
//
//   JELLYFIN_URL=http://localhost:8096 node src/run.mjs [--only name,name] [--out dir]
import { mkdir, writeFile } from 'node:fs/promises';
import { parseArgs } from 'node:util';
import { chromium } from 'playwright';
import { bootstrap, USERS } from './bootstrap.mjs';
import { scenarios } from './scenarios.mjs';
import { renderMarkdown } from './report.mjs';

const { values: args } = parseArgs({
  options: {
    only: { type: 'string' },
    out: { type: 'string', default: 'out' },
    url: { type: 'string', default: process.env.JELLYFIN_URL ?? 'http://localhost:8096' },
    headed: { type: 'boolean', default: false },
  },
});

const log = (msg) => console.log(`${new Date().toISOString().slice(11, 23)} ${msg}`);
await mkdir(args.out, { recursive: true });
const server = await bootstrap(args.url, { log });
const browser = await chromium.launch({
  headless: !args.headed,
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});

const selected = args.only ? scenarios.filter((s) => args.only.split(',').includes(s.name)) : scenarios;
const results = [];
for (const scenario of selected) {
  log(`=== ${scenario.name}: ${scenario.title}`);
  const ctx = { name: scenario.name, baseUrl: args.url, browser, server, users: USERS, log, outDir: args.out, checks: [], metrics: {}, cleanup: [] };
  const started = Date.now();
  let error = null;
  try {
    await scenario.run(ctx);
  } catch (e) {
    error = e.stack ?? String(e);
    log(`ERROR ${e.message}`);
  }
  for (const fn of ctx.cleanup.reverse()) await fn().catch(() => {});
  const passed = !error && ctx.checks.every((c) => c.passed);
  results.push({ name: scenario.name, title: scenario.title, passed, error, durationMs: Date.now() - started, checks: ctx.checks, metrics: ctx.metrics });
  log(`${passed ? 'PASS' : 'FAIL'} ${scenario.name}`);
}
await browser.close();

const report = {
  generatedAt: new Date().toISOString(),
  server: { url: args.url, version: server.serverVersion, itemId: server.itemId },
  thresholds: (await import('./scenarios.mjs')).THRESHOLDS,
  passed: results.every((r) => r.passed),
  scenarios: results,
};
await writeFile(`${args.out}/report.json`, JSON.stringify(report, null, 2));
await writeFile(`${args.out}/report.md`, renderMarkdown(report));
log(`Report: ${args.out}/report.md (${report.passed ? 'all passed' : 'FAILURES'})`);
process.exit(report.passed ? 0 : 1);
