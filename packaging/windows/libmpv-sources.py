#!/usr/bin/env python3
"""Source code and licences of everything inside the Windows libmpv-2.dll.

The MSI ships libmpv-2.dll from shinchiro/mpv-winbuild-cmake (pinned in
deps/versions.json, windows.mpv). That DLL is GPL-3.0-or-later and links about
ninety libraries statically, so every release must come with their licence
texts and their source code. The recipe builds each library from the head of a
branch and records no commit, so deps/libmpv-windows-sources.json records which
revision of each library the build used:

* "anchored": the DLL names the commit itself (a version string such as
  "libopus 1.6.1-81-g51bd2e6", or FFmpeg's "N-127617-g94cba3150"). Exact.
* otherwise the newest commit of the branch made before the build (the "before"
  time of the manifest's window). A commit can reach the server later than its
  date says, so the archive of such a library also carries the branch's history
  back to the window's "after" time: whichever of those revisions the build
  fetched, its exact source is in the archive.

Commands (Python 3.9+, git; "bundle" also needs tar, xz and cargo):

  resolve --recipe DIR --strings FILE [--write]
      Works out the revisions from a checkout of the recipe at the pinned
      commit and the printable strings of libmpv-2.dll (strings -n 6), and
      compares them with the manifest (or rewrites it with --write).
  bundle --out DIR [--licenses-only]
      Fetches every library at its revision into one archive each, with the
      recipe, a README and SHA256SUMS, and collects the licence texts into
      DIR/libmpv-windows.txt (the file packaging/licenses keeps).

Both run in .github/workflows/libmpv-sources.yml; see "libmpv for Windows:
source code" in packaging/README.md.
"""

import argparse
import concurrent.futures
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "deps" / "libmpv-windows-sources.json"
VERSIONS = ROOT / "deps" / "versions.json"

# ExternalProject_Add(<name> ... ) blocks of the recipe's packages/*.cmake.
PROJECT_RE = re.compile(r"ExternalProject_Add\((\S+)(.*?)\n\)", re.S)
# Keywords that end a DEPENDS list (any upper-case ExternalProject option).
DEPENDS_RE = re.compile(r"DEPENDS(.*?)\n\s*(?=[A-Z_]{3,}\s)", re.S)
# Variables the recipe puts in DEPENDS lists for x86_64 (packages/x265.cmake,
# cmake/packages_check.cmake).
DEPENDS_VARIABLES = {"ffmpeg_x265": "x265-8+10bit", "ffmpeg_uavs3d": "uavs3d", "ffmpeg_davs2": "davs2"}
# Version strings with a commit: "1.6.1-81-g51bd2e6", "N-127617-g94cba3150".
DESCRIBE_RE = re.compile(r"(?:^|[^0-9A-Za-z])((?:[0-9]+(?:\.[0-9]+)*|N)-[0-9]+-g([0-9a-f]{7,40}))\b")
LICENCE_NAME_RE = re.compile(
    r"^(licen[cs]e|copying|copyright|notice|unlicense|patents)([-._][\w.+-]*)?$", re.I)


def log(*args):
    print(*args, file=sys.stderr, flush=True)


def run(args, cwd=None, check=True, capture=True, env=None):
    result = subprocess.run(args, cwd=cwd, text=True, env=env,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if check and result.returncode != 0:
        raise RuntimeError(f"{' '.join(map(str, args))} failed ({result.returncode}): "
                           f"{(result.stderr or '')[-2000:]}")
    return (result.stdout or "").strip()


def git(directory, *args, check=True):
    return run(["git", "-C", str(directory), *args], check=check)


def load_manifest():
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def parse_time(text):
    return datetime.fromisoformat(text.replace("Z", "+00:00"))


# --- The recipe -------------------------------------------------------------

def recipe_projects(recipe):
    """name -> {depends, repository, branch, url, hash, submodules, file}"""
    projects = {}
    for path in sorted((recipe / "packages").glob("*.cmake")):
        text = path.read_text(encoding="utf-8")
        for match in PROJECT_RE.finditer(text):
            name, body = match.group(1), match.group(2)
            depends = []
            found = DEPENDS_RE.search(body)
            if found:
                for item in found.group(1).split():
                    variable = re.fullmatch(r"\$\{(\w+)\}", item)
                    if variable:
                        if variable.group(1) not in DEPENDS_VARIABLES:
                            raise RuntimeError(f"{path.name}: unknown DEPENDS variable {item}")
                        item = DEPENDS_VARIABLES[variable.group(1)]
                    depends.append(item)

            def option(key):
                found = re.search(r"\n\s*" + key + r"\s+(\S+)", body)
                return found.group(1).strip('"') if found else None

            projects[name] = {
                "depends": depends,
                "repository": option("GIT_REPOSITORY"),
                "branch": option("GIT_TAG"),
                "url": option("URL"),
                "hash": option("URL_HASH"),
                # Unset: ExternalProject checks out every submodule; "" none.
                "submodules": option("GIT_SUBMODULES"),
                "sparse": re.search(r'sparse-checkout set --no-cone ([^"\n]*)', body),
                "file": path.name,
            }
    return projects


def closure(projects, root="mpv"):
    """mpv and everything it is built with. Projects without a source of their
    own (the x265 bit-depth builds) count as the project whose source they use."""
    seen = []

    def walk(name):
        if name in seen:
            return
        if name not in projects:
            raise RuntimeError(f"the recipe has no project {name}")
        seen.append(name)
        for dependency in projects[name]["depends"]:
            walk(dependency)

    walk(root)
    sources = []
    for name in seen:
        project = projects[name]
        if project["repository"] or project["url"]:
            sources.append(name)
        elif name.startswith("x265"):
            sources.append("x265")
        else:
            raise RuntimeError(f"{name} ({project['file']}) has no source")
    return sorted(set(sources))


# --- resolve ----------------------------------------------------------------

def clone_metadata(repository, directory):
    if not (directory / "HEAD").exists():
        run(["git", "clone", "-q", "--bare", "--filter=tree:0", repository, str(directory)])
    return directory


def resolve(args):
    manifest = load_manifest()
    versions = json.loads(VERSIONS.read_text(encoding="utf-8"))
    pin = versions["windows"]["mpv"]
    if manifest["build"] != pin["version"]:
        raise SystemExit(f"manifest build {manifest['build']} is not deps/versions.json "
                         f"windows.mpv.version {pin['version']}")
    recipe = Path(args.recipe)
    if git(recipe, "rev-parse", "HEAD") != manifest["recipe"]["commit"]:
        raise SystemExit("--recipe is not checked out at the manifest's recipe commit")
    projects = recipe_projects(recipe)
    names = closure(projects)
    strings = Path(args.strings).read_text(encoding="utf-8", errors="replace").splitlines()
    describes = sorted({m.group(1) for line in strings for m in DESCRIBE_RE.finditer(line)})
    log(f"{len(names)} libraries; version strings with a commit: {', '.join(describes)}")
    before = manifest["window"]["before"]
    after = manifest["window"]["after"]
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)

    def one(name):
        project = projects[name]
        entry = {"name": name, "recipe": f"packages/{project['file']}"}
        if project["url"]:
            algorithm, _, digest = project["hash"].partition("=")
            entry.update(url=project["url"], hash=f"{algorithm.lower()}:{digest.lower()}")
            return entry
        repository = project["repository"]
        override = manifest.get("fetchFrom", {}).get(name)
        bare = clone_metadata(override or repository, work / f"{name}.git")
        branch = project["branch"] or git(bare, "symbolic-ref", "--short", "HEAD")
        entry.update(repository=repository, branch=branch)
        anchor = None
        for text in describes:
            short = DESCRIBE_RE.search(" " + text).group(2)
            commit = git(bare, "rev-parse", "-q", "--verify", f"{short}^{{commit}}", check=False)
            if commit and subprocess.run(["git", "-C", str(bare), "merge-base", "--is-ancestor",
                                          commit, branch]).returncode == 0:
                if anchor:
                    raise RuntimeError(f"{name}: both {anchor[0]} and {text} name its commits")
                anchor = (text, commit)
        if anchor:
            entry.update(commit=anchor[1], anchor=anchor[0])
        else:
            commit = git(bare, "rev-list", "-1", "--first-parent", f"--before={before}", branch)
            if not commit:
                raise RuntimeError(f"{name}: no commit on {branch} before {before}")
            entry.update(commit=commit, historySince=after)
        entry["date"] = git(bare, "log", "-1", "--format=%cI", entry["commit"])
        submodules = project["submodules"]
        if submodules is None:
            entry["submodules"] = "all"
        elif submodules != "":
            entry["submodules"] = submodules.split(";")
        # A sparse checkout limited to some folders (headers); "/*" is the
        # whole tree (its exclusions are kept: more source does no harm).
        if project["sparse"]:
            paths = [p.strip("/") for p in project["sparse"].group(1).split() if not p.startswith("!")]
            if "*" not in paths:
                entry["paths"] = paths
        return entry

    def guarded(name):
        try:
            return one(name)
        except RuntimeError as error:
            return {"name": name, "error": str(error)}

    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        components = list(pool.map(guarded, names))
    failed = [c for c in components if "error" in c]
    for component in failed:
        log(f"{component['name']}: {component['error']}")
    if failed:
        raise SystemExit(f"{len(failed)} libraries could not be resolved")
    used = {c.get("anchor") for c in components}
    unused = [d for d in describes if d not in used]
    if unused:
        log(f"version strings that name no library's commit: {', '.join(unused)}")
    old = manifest.get("components")
    manifest["components"] = components
    text = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"
    if args.write:
        MANIFEST.write_text(text, encoding="utf-8")
        log(f"wrote {MANIFEST.relative_to(ROOT)}")
        return
    if old != components:
        print(text)
        raise SystemExit(f"{MANIFEST.relative_to(ROOT)} does not match the recipe and the DLL "
                         "(the resolved manifest is printed above)")
    log(f"{MANIFEST.relative_to(ROOT)} matches: {len(components)} libraries, "
        f"{sum('anchor' in c for c in components)} named by the DLL itself")


# --- bundle -----------------------------------------------------------------

def download(url, target, expected):
    algorithm, _, digest = expected.partition(":")
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=300) as response, open(target, "wb") as out:
                shutil.copyfileobj(response, out)
            break
        except OSError:
            if attempt == 3:
                raise
    actual = hashlib.new(algorithm, target.read_bytes()).hexdigest()
    if actual != digest:
        raise RuntimeError(f"{url}: {algorithm} {actual}, pinned {digest}")


def fetch_component(component, directory):
    """The library's tree at its commit (and, for an unanchored one, the
    branch's history since the window's start) in DIRECTORY."""
    run(["git", "init", "-q", str(directory)])
    git(directory, "remote", "add", "origin", component["repository"])
    commit = component["commit"]
    for attempt in range(4):
        try:
            if "historySince" in component:
                # The branch since the window's start (servers that send a
                # commit by its hash only with --depth take the branch name).
                git(directory, "fetch", "-q", f"--shallow-since={component['historySince']}",
                    "origin", f"+refs/heads/{component['branch']}:refs/remotes/origin/{component['branch']}")
                if git(directory, "cat-file", "-t", commit, check=False) != "commit":
                    git(directory, "fetch", "-q", "--depth=1", "origin", commit)
            else:
                git(directory, "fetch", "-q", "--depth=1", "origin", commit)
            break
        except RuntimeError:
            if attempt == 3:
                raise
    git(directory, "-c", "advice.detachedHead=false", "checkout", "-q", commit)
    if "historySince" in component:
        # The revisions the archive offers: branch "build", from the commit
        # back to the window's start. The branch's newer commits are dropped.
        git(directory, "checkout", "-q", "-B", "build", commit)
        git(directory, "update-ref", "-d", f"refs/remotes/origin/{component['branch']}")
        git(directory, "remote", "remove", "origin")
        git(directory, "remote", "add", "origin", component["repository"])
        git(directory, "reflog", "expire", "--expire=now", "--all")
        git(directory, "gc", "-q", "--prune=now")
    submodules = component.get("submodules")
    if submodules == "all":
        if (directory / ".gitmodules").exists():
            git(directory, "submodule", "update", "-q", "--init", "--recursive", "--depth=1")
    elif submodules:
        git(directory, "submodule", "update", "-q", "--init", "--recursive", "--depth=1", "--", *submodules)
    if component.get("paths"):
        # Only these folders are used: keep them, and the changes the
        # branch's history made to them since the window's start.
        full = directory.with_name(directory.name + ".full")
        directory.rename(full)
        for path in component["paths"]:
            shutil.copytree(full / path, directory / path, symlinks=True)
        for path in full.iterdir():
            if path.is_file() and LICENCE_NAME_RE.match(path.name):
                shutil.copy2(path, directory / path.name)
        if "historySince" in component:
            log_text = git(full, "log", "-p", "--first-parent", "--format=commit %H%nDate: %cI%n%n%w(0,4,4)%B",
                           commit, "--", *component["paths"])
            (directory / "HISTORY.patch").write_text(log_text + "\n", encoding="utf-8")
        shutil.rmtree(full)
    elif "historySince" not in component:
        for gitfile in [directory / ".git", *directory.rglob(".git")]:
            if gitfile.is_dir():
                shutil.rmtree(gitfile)
            elif gitfile.exists():
                gitfile.unlink()


def vendor_cargo(directory):
    """Rust crates the library builds with (subrandr), from its Cargo.lock."""
    if not (directory / "Cargo.lock").exists():
        return
    config = run(["cargo", "vendor", "--locked", "--versioned-dirs", "vendor"], cwd=directory)
    (directory / ".cargo").mkdir(exist_ok=True)
    settings = directory / ".cargo" / "config.toml"
    settings.write_text(config + "\n", encoding="utf-8")
    # Every crate of the lock file stays (cargo resolves them all), but only
    # those of the Windows build and its build tool are in the DLL: their
    # licences are collected (licence_files).
    used = set()
    for package in ("subrandr", "xtask"):
        tree = run(["cargo", "tree", "--offline", "--locked", "-e", "normal,build", "--target",
                    "x86_64-pc-windows-gnullvm", "-p", package, "--prefix", "none", "--format", "{p}"],
                   cwd=directory)
        for line in tree.splitlines():
            name, _, version = line.split(" (")[0].strip().partition(" v")
            if (directory / "vendor" / f"{name}-{version}").is_dir():
                used.add(f"{name}-{version}")
    (directory / "vendor" / "WINDOWS-BUILD-CRATES.txt").write_text(
        "".join(f"{name}\n" for name in sorted(used)), encoding="utf-8")
    settings.rename(directory / ".cargo" / "config.toml.vendored")


def licence_files(tree):
    """Licence texts at the top of the tree, of each submodule and vendored
    crate, and in their LICENSES/ folders."""
    roots = [tree]
    gitmodules = tree / ".gitmodules"
    if gitmodules.exists():
        for path in re.findall(r"^\s*path\s*=\s*(.+?)\s*$", gitmodules.read_text(encoding="utf-8"), re.M):
            roots.append(tree / path)
    crates = tree / "vendor" / "WINDOWS-BUILD-CRATES.txt"
    if crates.exists():
        roots += [tree / "vendor" / name for name in crates.read_text(encoding="utf-8").split()]
    found = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in sorted(root.iterdir()):
            if path.is_file() and LICENCE_NAME_RE.match(path.name) and path.stat().st_size < 400_000:
                found.append(path)
        spdx = root / "LICENSES"
        if spdx.is_dir():
            found += sorted(p for p in spdx.iterdir() if p.is_file())
    return found


def fetch_licence_paths(entry, directory):
    """Only the named files of a repository at a branch or commit (the
    toolchain's runtimes, whose licences the DLL carries)."""
    run(["git", "init", "-q", str(directory)])
    git(directory, "remote", "add", "origin", entry["repository"])
    git(directory, "sparse-checkout", "set", "--no-cone", *entry["paths"])
    git(directory, "fetch", "-q", "--depth=1", "--filter=blob:none", "origin", entry["revision"])
    git(directory, "-c", "advice.detachedHead=false", "checkout", "-q", "FETCH_HEAD")
    commit = git(directory, "rev-parse", "HEAD")
    texts = [(path, (directory / path).read_text(encoding="utf-8", errors="replace"))
             for path in entry["paths"]]
    shutil.rmtree(directory)
    return commit, texts


def deterministic_tar(source, target, mtime):
    """An xz tarball of SOURCE (top folder SOURCE's name) that does not depend
    on when or by whom it was made."""
    def clean(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mtime = mtime
        return info

    with tarfile.open(target, "w:xz", preset=6) as archive:
        archive.add(source, arcname=source.name, filter=clean, recursive=False)
        for path in sorted(source.rglob("*")):
            archive.add(path, arcname=f"{source.name}/{path.relative_to(source)}", filter=clean,
                        recursive=False)


def bundle(args):
    manifest = load_manifest()
    out = Path(args.out)
    assets = out / "assets"
    work = Path(args.work)
    for path in (assets, work):
        path.mkdir(parents=True, exist_ok=True)
    components = manifest["components"]
    build = manifest["build"]

    def one(component):
        name = component["name"]
        tree = work / name
        if tree.exists():
            shutil.rmtree(tree)
        if "url" in component:
            filename = component["url"].rsplit("/", 1)[1]
            archive = assets / f"{name}--{filename}"
            download(component["url"], archive, component["hash"])
            tree.mkdir()
            with tarfile.open(archive) as source:
                source.extractall(tree, filter="data")
            asset = archive.name
        else:
            fetch_component(component, tree)
            if name == "subrandr":
                vendor_cargo(tree)
            asset = f"{name}--{component['commit'][:12]}.tar.xz"
            if not args.licenses_only:
                mtime = int(parse_time(component["date"]).timestamp())
                deterministic_tar(tree, assets / asset, mtime)
        texts = [(str(p.relative_to(tree)), p.read_text(encoding="utf-8", errors="replace"))
                 for p in licence_files(tree)]
        # Licences that are only in source files: "path:N" is its first N lines.
        for item in manifest.get("licenseFiles", {}).get(name, []):
            path, _, lines = item.partition(":")
            text = (tree / path).read_text(encoding="utf-8", errors="replace")
            if lines:
                text = "".join(text.splitlines(keepends=True)[:int(lines)])
            texts.append((item, text))
        if not texts:
            raise RuntimeError(f"{name}: no licence file found; name them in the manifest's \"licenseFiles\"")
        shutil.rmtree(tree)
        log(f"{name}: {asset}, {len(texts)} licence file(s)")
        return name, asset, texts

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(one, components))

    # The recipe itself, at its commit.
    recipe = manifest["recipe"]
    recipe_tree = work / "mpv-winbuild-cmake"
    if recipe_tree.exists():
        shutil.rmtree(recipe_tree)
    fetch_component({"repository": recipe["repository"], "commit": recipe["commit"]}, recipe_tree)
    recipe_texts = [(str(p.relative_to(recipe_tree)), p.read_text(encoding="utf-8", errors="replace"))
                    for p in licence_files(recipe_tree)]
    recipe_asset = f"mpv-winbuild-cmake--{recipe['commit'][:12]}.tar.xz"
    if not args.licenses_only:
        deterministic_tar(recipe_tree, assets / recipe_asset, int(parse_time(recipe["date"]).timestamp()))
    shutil.rmtree(recipe_tree)

    toolchain = []
    for entry in manifest.get("toolchain", []):
        commit, texts = fetch_licence_paths(entry, work / f"toolchain-{entry['name']}")
        log(f"{entry['name']}: licences at {commit}")
        toolchain.append((entry, texts))

    write_licences(out / "libmpv-windows.txt", manifest, results, recipe_texts, toolchain)
    if args.licenses_only:
        return
    # The DLL's package itself: its upstream release is deleted once 30 newer
    # builds exist, and scripts/fetch-windows-deps.ps1 falls back to this copy.
    pin = json.loads(VERSIONS.read_text(encoding="utf-8"))["windows"]["mpv"]
    download(pin["url"], assets / pin["name"], f"sha256:{pin['sha256']}")
    write_readme(out / "README.txt", manifest, results, recipe_asset)
    shutil.copy(out / "README.txt", assets / "README.txt")
    shutil.copy(out / "libmpv-windows.txt", assets / "LICENSES-libmpv-windows.txt")
    shutil.copy(MANIFEST, assets / MANIFEST.name)
    sums = []
    for path in sorted(assets.iterdir()):
        digest = hashlib.sha256()
        with open(path, "rb") as stream:
            for block in iter(lambda: stream.read(1 << 20), b""):
                digest.update(block)
        sums.append(f"{digest.hexdigest()}  {path.name}")
    (assets / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")
    log(f"{len(sums)} files in {assets} for libmpv {build}")


def write_licences(target, manifest, results, recipe_texts, toolchain):
    lines = [
        f"Licences of the libraries inside libmpv-2.dll ({manifest['build']})",
        "=" * 72,
        "",
        "libmpv-2.dll in the Windows package is the libmpv build "
        f"{manifest['build']} of",
        f"{manifest['recipe']['repository']} (recipe commit {manifest['recipe']['commit']}).",
        "It is GPL-3.0-or-later as a whole (GPL-3.0.txt). Below are the licence",
        "files of every library it is built from, as they are in each library's",
        "source at the revision the build used. The source code of all of them is",
        "published with the program: see \"Where the sources are\" in",
        "THIRD-PARTY-NOTICES.txt.",
        "",
        "Libraries:",
    ]
    for component in manifest["components"]:
        revision = component["commit"][:12] if "commit" in component else component["url"].rsplit("/", 1)[1]
        lines.append(f"  {component['name']:<17} {revision}")
    for entry in manifest.get("toolchain", []):
        lines.append(f"  {entry['name']:<17} {entry['revision']} ({entry['comment']})")
    lines.append("")
    seen = {}

    def section(title, texts):
        lines.extend(["", "#" * 72, f"# {title}", "#" * 72])
        if not texts:
            lines.extend(["", "(no licence file)"])
        for path, text in texts:
            # Texts that differ only in spacing are the same licence.
            digest = hashlib.sha256(" ".join(text.split()).encode("utf-8")).hexdigest()
            lines.extend(["", f"--- {path} ---"])
            if digest in seen:
                lines.append(f"(the same text as {seen[digest]} above)")
                continue
            seen[digest] = f"{title.split(' ')[0]}: {path}"
            lines.append(text.rstrip("\n"))

    for name, _asset, texts in sorted(results):
        component = next(c for c in manifest["components"] if c["name"] == name)
        where = component.get("repository") or component.get("url")
        revision = component.get("commit", component.get("hash", ""))
        section(f"{name} ({where} {revision})", texts)
    section(f"mpv-winbuild-cmake ({manifest['recipe']['repository']} {manifest['recipe']['commit']}, the recipe)",
            recipe_texts)
    for entry, texts in toolchain:
        section(f"{entry['name']} ({entry['repository']} {entry['revision']}, {entry['comment']})", texts)
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_readme(target, manifest, results, recipe_asset):
    build = manifest["build"]
    window = manifest["window"]
    lines = [
        f"Source code of libmpv-2.dll {build}",
        "=" * 72,
        "",
        "Jellyfin Desktop Next's Windows package (MSI) contains libmpv-2.dll, the",
        f"libmpv build {build} of {manifest['recipe']['repository']},",
        "unmodified. This release holds the complete corresponding source of that",
        "DLL: every library it is built from, and the build recipe.",
        "",
        f"{recipe_asset}",
        f"    the recipe ({manifest['recipe']['repository']} at",
        f"    {manifest['recipe']['commit']}): CMake files, patches, toolchain set-up.",
        "    Build with -DTARGET_ARCH=x86_64-w64-mingw32 -DCOMPILER_TOOLCHAIN=clang",
        "    (its .github/workflows/mpv_clang.yml) after unpacking each library below",
        "    into src_packages/<name>.",
        "",
        "One archive per library, named <library>--<commit or file>:",
        "",
    ]
    for name, asset, _texts in sorted(results):
        component = next(c for c in manifest["components"] if c["name"] == name)
        if "url" in component:
            note = f"{component['url']} ({component['hash']})"
        elif "anchor" in component:
            note = (f"{component['repository']} {component['commit']}: exact, the DLL names it "
                    f"(\"{component['anchor']}\")")
        else:
            note = (f"{component['repository']} branch {component['branch']} at {component['commit']} "
                    f"with its history since {component['historySince']}")
        lines.append(f"{asset}")
        lines.append(f"    {note}")
    lines += [
        "",
        "Which revision",
        "--------------",
        "The recipe builds each library from the head of a branch and does not",
        "record the commit. Where the DLL names the commit in a version string, the",
        "archive is that commit. Elsewhere it is the newest commit of the branch",
        f"dated before {window['before']} ({window['comment']}),",
        "and it is a git repository with the branch's history since",
        f"{window['after']}: a commit can reach the server later than its date, so",
        "the revision the build fetched is the checked-out one or one of the older",
        "commits in that history (git log; git checkout <commit>).",
        "",
        "Toolchain: the build's clang, LLVM's libc++, libunwind and compiler-rt, the",
        "mingw-w64 runtime and the Rust standard library are the compiler's and its",
        "runtime's; their licence texts are in LICENSES-libmpv-windows.txt and",
        "their sources are at the repositories named there.",
        "",
        "LICENSES-libmpv-windows.txt   the licence texts of every library",
        f"{MANIFEST.name:<29} the revisions, machine-readable",
        "SHA256SUMS                    checksums of the files here",
    ]
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    resolve_parser = commands.add_parser("resolve")
    resolve_parser.add_argument("--recipe", required=True)
    resolve_parser.add_argument("--strings", required=True)
    resolve_parser.add_argument("--work", default="build/libmpv-sources/metadata")
    resolve_parser.add_argument("--write", action="store_true")
    bundle_parser = commands.add_parser("bundle")
    bundle_parser.add_argument("--out", required=True)
    bundle_parser.add_argument("--work", default="build/libmpv-sources/trees")
    bundle_parser.add_argument("--jobs", type=int, default=6)
    bundle_parser.add_argument("--licenses-only", action="store_true")
    args = parser.parse_args()
    {"resolve": resolve, "bundle": bundle}[args.command](args)


if __name__ == "__main__":
    main()
