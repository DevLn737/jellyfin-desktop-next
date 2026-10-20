// Position sampling across participants. All participants run on one machine,
// so Date.now() is a shared clock: each sample is projected to a common
// instant using the rate it reported, which removes sampling skew.
import { sleep } from './jellyfin.mjs';

export async function snapshot(participants) {
  const samples = await Promise.all(participants.map(async (p) => ({ label: p.label, ...(await p.sample()) })));
  const at = Math.max(...samples.map((s) => s.at));
  for (const s of samples) {
    s.projectedMs = s.positionMs == null ? null : s.positionMs + (at - s.at) * (s.playing ? s.rate : 0);
  }
  const values = samples.map((s) => s.projectedMs).filter((v) => v != null);
  const spreadMs = values.length === samples.length ? Math.max(...values) - Math.min(...values) : null;
  return { at, samples, spreadMs };
}

/** Polls until predicate(snapshot) is true; returns the snapshot and elapsed ms. */
export async function waitUntil(participants, predicate, timeoutMs, intervalMs = 100) {
  const started = Date.now();
  let last = null;
  while (Date.now() - started < timeoutMs) {
    last = await snapshot(participants);
    if (predicate(last)) return { ok: true, elapsedMs: Date.now() - started, snapshot: last };
    await sleep(intervalMs);
  }
  return { ok: false, elapsedMs: Date.now() - started, snapshot: last };
}

export const allPlaying = (s) => s.samples.every((x) => x.playing);
export const allPaused = (s) => s.samples.every((x) => !x.playing && x.positionMs != null);
export const round = (v, d = 0) => (v == null ? null : Number(v.toFixed(d)));
