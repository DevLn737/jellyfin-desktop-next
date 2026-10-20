// Turns a fresh test server into a known state: wizard done, admin + test
// users, one movie library with the generated clip, scan finished.
import { JellyfinHttp, waitForHealth, sleep } from './jellyfin.mjs';

export const ADMIN = { name: 'admin', password: 'admin' };
export const USERS = ['alice', 'bob', 'carol', 'dave'].map((name) => ({ name, password: name }));
export const CLIP_NAME = 'SyncPlay Test Clip';

export async function bootstrap(baseUrl, { log = console.log } = {}) {
  await waitForHealth(baseUrl);
  const http = new JellyfinHttp(baseUrl, { device: 'bootstrap' });
  const info = await http.get('System/Info/Public');
  if (!info.StartupWizardCompleted) {
    log('Completing the startup wizard');
    await http.post('Startup/Configuration', { UICulture: 'en-US', MetadataCountryCode: 'US', PreferredMetadataLanguage: 'en' });
    await http.get('Startup/User');
    await http.post('Startup/User', { Name: ADMIN.name, Password: ADMIN.password });
    await http.post('Startup/RemoteAccess', { EnableRemoteAccess: true, EnableAutomaticPortMapping: false });
    await http.post('Startup/Complete');
  }
  await http.login(ADMIN.name, ADMIN.password);

  // Jellyfin 12.2 turns legacy authorization off on upgrade and for new servers,
  // but Jellyfin Web 12.2 (jellyfin-apiclient 1.11.0) still opens its WebSocket
  // with ?api_key=, which then gets HTTP 403: no WebSocket, no SyncPlay in Web.
  // Production servers that work with Web must have it enabled, so mirror that.
  const legacyAuthorization = process.env.HARNESS_LEGACY_AUTH !== '0';
  const config = await http.get('System/Configuration');
  if (config.EnableLegacyAuthorization !== legacyAuthorization) {
    log(`Setting EnableLegacyAuthorization=${legacyAuthorization}`);
    await http.post('System/Configuration', { ...config, EnableLegacyAuthorization: legacyAuthorization });
  }

  const existing = new Set((await http.get('Users')).map((u) => u.Name));
  for (const user of USERS) {
    if (!existing.has(user.name)) {
      log(`Creating user ${user.name}`);
      await http.post('Users/New', { Name: user.name, Password: user.password });
    }
  }

  const folders = await http.get('Library/VirtualFolders');
  if (!folders.some((f) => f.Name === 'Movies')) {
    log('Adding the Movies library');
    await http.post('Library/VirtualFolders?name=Movies&collectionType=movies&refreshLibrary=true', {
      LibraryOptions: {
        EnableRealtimeMonitor: false,
        EnableChapterImageExtraction: false,
        ExtractChapterImagesDuringLibraryScan: false,
        EnableTrickplayImageExtraction: false,
        ExtractTrickplayImagesDuringLibraryScan: false,
        EnableInternetProviders: false,
        MetadataSavers: [],
        TypeOptions: [],
        PathInfos: [{ Path: '/media/movies' }],
      },
    });
  }

  const deadline = Date.now() + 120_000;
  while (Date.now() < deadline) {
    const items = await http.get(`Items?Recursive=true&IncludeItemTypes=Movie&Fields=MediaSources`);
    const clip = items.Items?.find((i) => i.Name.startsWith(CLIP_NAME));
    if (clip && clip.RunTimeTicks) {
      log(`Library ready: ${clip.Name} (${clip.Id}), ${(clip.RunTimeTicks / 1e7).toFixed(0)} s`);
      return { itemId: clip.Id, runTimeTicks: clip.RunTimeTicks, serverVersion: info.Version, legacyAuthorization };
    }
    await sleep(1000);
  }
  throw new Error('The test clip did not appear in the library');
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const baseUrl = process.env.JELLYFIN_URL ?? 'http://localhost:8096';
  console.log(JSON.stringify(await bootstrap(baseUrl), null, 2));
}
