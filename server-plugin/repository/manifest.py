"""Writes a Jellyfin plugin repository manifest.

    python3 manifest.py <base url> <dist dir>

Every <slug>_<version>.zip in <dist dir> becomes the one version offered of
the plugin <slug> in plugins.json; its download URL is <base url>/<zip>.
"""
import datetime
import hashlib
import json
import pathlib
import sys

base, dist = sys.argv[1].rstrip("/"), pathlib.Path(sys.argv[2])
plugins = json.loads((pathlib.Path(__file__).parent / "plugins.json").read_text())
now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
manifest = []
for zip_path in sorted(dist.glob("*.zip")):
    slug, version = zip_path.stem.rsplit("_", 1)
    info = plugins[slug]
    manifest.append({
        "guid": info["guid"],
        "name": info["name"],
        "description": info["description"],
        "overview": info["overview"],
        "owner": info["owner"],
        "category": info["category"],
        "versions": [{
            "version": version,
            "changelog": info["changelog"],
            "targetAbi": info["targetAbi"],
            "sourceUrl": f"{base}/{zip_path.name}",
            "checksum": hashlib.md5(zip_path.read_bytes()).hexdigest(),
            "timestamp": now,
        }],
    })
if len(manifest) != len(plugins):
    sys.exit(f"expected {len(plugins)} plugin zips in {dist}, found {len(manifest)}")
print(json.dumps(manifest, indent=2))
