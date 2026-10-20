// A participant measured from the screen: any client (a native build, Fathom,
// a future jellyfin-desktop-next binary) that shows the test clip on an X
// display. The clip's frame barcode gives the shown frame; consecutive
// samples give the play state and rate. The client itself joins the group by
// its own means; this class only observes it.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

export const CLIP_FPS = 24;

export class ScreenParticipant {
  /**
   * @param {{label: string, display?: string, x: number, y: number, cell: number}} options
   *   x, y: screen position of the barcode strip; cell: on-screen width of one cell.
   */
  constructor({ label, display = process.env.DISPLAY ?? ':99', x, y, cell, row = 0.5 }) {
    this.kind = 'screen';
    this.label = label;
    const script = fileURLToPath(new URL('./screen_probe.py', import.meta.url));
    this.proc = spawn('python3', [script, '--display', display, '--x', String(x), '--y', String(y), '--cell', String(cell), '--row', String(row)], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.lines = createInterface({ input: this.proc.stdout });
    this.pending = [];
    this.lines.on('line', (line) => this.pending.shift()?.(JSON.parse(line)));
    this.last = null;
  }

  probe() {
    return new Promise((resolve) => {
      this.pending.push(resolve);
      this.proc.stdin.write('\n');
    });
  }

  async start() {}

  async sample() {
    // Two probes ~250 ms apart give the rate; the later one is the position.
    const a = await this.probe();
    await new Promise((r) => setTimeout(r, 250));
    const b = await this.probe();
    if (a.frame == null || b.frame == null) return { at: b.atMs, positionMs: null, playing: false, rate: 0, baseRate: null };
    const mediaMs = ((b.frame - a.frame) / CLIP_FPS) * 1000;
    const rate = mediaMs / (b.atMs - a.atMs);
    // The frame on screen is shown for 1/fps; take the middle of that interval.
    const positionMs = ((b.frame + 0.5) / CLIP_FPS) * 1000;
    const playing = rate > 0.2;
    return { at: Math.round(b.atMs), positionMs, playing, rate: playing ? rate : 0, baseRate: playing ? rate : null, frame: b.frame };
  }

  async stop() {
    this.proc.stdin.end();
    this.proc.kill();
  }
}
