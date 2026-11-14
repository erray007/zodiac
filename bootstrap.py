#!/usr/bin/env python3
"""One-command bootstrap for Zodiac: downloads pinned CMake, Ninja and llvm-mingw into .tools/."""

import hashlib
import json
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

MANIFEST_RELATIVE = Path("tools") / "toolchain.json"
MIN_CLANG_MAJOR = 17
CHUNK_SIZE = 1024 * 256


class ChecksumError(Exception):
    """Raised when a downloaded file does not match its pinned SHA-256."""


class UnsafeArchiveError(Exception):
    """Raised when an archive member would be extracted outside the destination."""


def say(message: str) -> None:
    print(message, flush=True)


def load_manifest(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def download(url: str, dest: Path, sha256: str) -> None:
    """Download url to dest, verifying SHA-256. The partial file is removed on any failure."""
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = dest.with_name(dest.name + ".part")
    digest = hashlib.sha256()
    try:
        with urllib.request.urlopen(url) as response, open(partial, "wb") as out:
            total = int(response.headers.get("Content-Length") or 0)
            received = 0
            last_step = -1
            while True:
                chunk = response.read(CHUNK_SIZE)
                if not chunk:
                    break
                out.write(chunk)
                digest.update(chunk)
                received += len(chunk)
                if total:
                    step = received * 10 // total
                    if step != last_step:
                        last_step = step
                        say(f"Downloading {dest.name}: {step * 10} percent")
        actual = digest.hexdigest()
        if actual.lower() != sha256.lower():
            raise ChecksumError(f"Checksum mismatch for {dest.name}: expected {sha256}, got {actual}")
        os.replace(partial, dest)
    except BaseException:
        partial.unlink(missing_ok=True)
        raise


def _strip_member(name: str, strip_prefix: str):
    """Validate a member name and return its destination-relative parts, or None to skip it."""
    normalized = name.replace("\\", "/")
    pure = PurePosixPath(normalized)
    if normalized.startswith("/") or pure.is_absolute() or re.match(r"^[A-Za-z]:", normalized):
        raise UnsafeArchiveError(f"Absolute path in archive: {name}")
    if ".." in pure.parts:
        raise UnsafeArchiveError(f"Parent path in archive: {name}")
    parts = [part for part in pure.parts if part != "."]
    prefix_parts = [part for part in PurePosixPath(strip_prefix).parts if part != "."] if strip_prefix else []
    if prefix_parts:
        if parts[: len(prefix_parts)] != prefix_parts:
            raise UnsafeArchiveError(f"Archive member outside expected prefix '{strip_prefix}': {name}")
        parts = parts[len(prefix_parts):]
    return parts or None


def _target_path(dest_root: Path, parts) -> Path:
    target = dest_root.joinpath(*parts)
    resolved = target.resolve()
    if resolved != dest_root and dest_root not in resolved.parents:
        raise UnsafeArchiveError(f"Archive member escapes destination: {'/'.join(parts)}")
    return target


def safe_extract(archive: Path, dest: Path, strip_prefix: str) -> None:
    """Extract a zip or tar archive into dest, dropping strip_prefix and refusing unsafe members."""
    archive = Path(archive)
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    dest_root = dest.resolve()
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as zf:
            plan = []
            for info in zf.infolist():
                parts = _strip_member(info.filename, strip_prefix)
                if parts is not None:
                    plan.append((info, _target_path(dest_root, parts)))
            for info, target in plan:
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with zf.open(info) as src, open(target, "wb") as out:
                    shutil.copyfileobj(src, out)
                mode = info.external_attr >> 16
                if mode & stat.S_IXUSR:
                    target.chmod(target.stat().st_mode | 0o111)
    else:
        with tarfile.open(archive, "r:*") as tf:
            plan = []
            for member in tf.getmembers():
                parts = _strip_member(member.name, strip_prefix)
                if parts is None:
                    continue
                if member.issym() or member.islnk():
                    link_parts = _strip_member(member.linkname, "") if member.islnk() else None
                    if member.issym():
                        link = PurePosixPath(member.linkname)
                        if link.is_absolute():
                            raise UnsafeArchiveError(f"Absolute symlink in archive: {member.name}")
                        base = PurePosixPath(*parts).parent / link
                        resolved = os.path.normpath(str(base)).replace("\\", "/")
                        if resolved.startswith(".."):
                            raise UnsafeArchiveError(f"Symlink escapes destination: {member.name}")
                    elif link_parts is None:
                        raise UnsafeArchiveError(f"Bad hard link in archive: {member.name}")
                elif not (member.isfile() or member.isdir()):
                    raise UnsafeArchiveError(f"Unsupported member type in archive: {member.name}")
                plan.append((member, _target_path(dest_root, parts), parts))
            for member, target, parts in plan:
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                if member.issym():
                    if target.is_symlink() or target.exists():
                        target.unlink()
                    os.symlink(member.linkname, target)
                elif member.islnk():
                    source = _target_path(dest_root, _strip_member(member.linkname, strip_prefix) or [])
                    shutil.copyfile(source, target)
                    target.chmod(member.mode | 0o600)
                else:
                    src = tf.extractfile(member)
                    with src, open(target, "wb") as out:
                        shutil.copyfileobj(src, out)
                    target.chmod(member.mode | 0o600)


def ensure_tool(name: str, spec: dict, root: Path) -> bool:
    """Install one pinned tool under root/.tools/name. Returns False if already up to date."""
    root = Path(root)
    tools_dir = root / ".tools"
    target = tools_dir / name
    stamp = target / ".stamp"
    version = str(spec["version"])
    if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() == version:
        return False
    cache = tools_dir / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / spec["url"].rsplit("/", 1)[-1]
    if not (archive.is_file() and _sha256_of(archive) == spec["sha256"].lower()):
        say(f"Downloading {name} {version}")
        download(spec["url"], archive, spec["sha256"])
    say(f"Extracting {name} {version}")
    staging = tools_dir / (name + ".new")
    if staging.exists():
        shutil.rmtree(staging)
    safe_extract(archive, staging, spec.get("strip_prefix", ""))
    (staging / ".stamp").write_text(version + "\n", encoding="utf-8")
    if target.exists():
        shutil.rmtree(target)
    os.replace(staging, target)
    return True


def _sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(CHUNK_SIZE), b""):
            digest.update(chunk)
    return digest.hexdigest()


def detect_host() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        machine = "x86_64"
    return f"{system}-{machine}"


def clang_major_version():
    clang = shutil.which(os.environ.get("CC") or "clang")
    if not clang:
        return None
    try:
        output = subprocess.run([clang, "--version"], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    match = re.search(r"clang version (\d+)", output)
    return int(match.group(1)) if match else None


def check_linux_clang() -> bool:
    major = clang_major_version()
    if major is not None and major >= MIN_CLANG_MAJOR:
        return True
    found = "not found" if major is None else f"version {major} is too old"
    say(f"clang {MIN_CLANG_MAJOR} or newer is required ({found}).")
    say("Install it with: sudo apt install clang-17 (Debian or Ubuntu), sudo dnf install clang (Fedora), or sudo pacman -S clang (Arch).")
    return False


def main() -> int:
    root = Path(__file__).resolve().parent
    manifest = load_manifest(root / MANIFEST_RELATIVE)
    host = detect_host()
    specs = manifest.get("hosts", {}).get(host)
    if specs is None:
        say(f"Unsupported host: {host}. Supported hosts: {', '.join(sorted(manifest.get('hosts', {})))}.")
        return 1
    try:
        for name, spec in specs.items():
            if not ensure_tool(name, spec, root):
                say(f"{name} {spec['version']} is already installed.")
    except (ChecksumError, UnsafeArchiveError) as error:
        say(f"Error: {error}")
        return 1
    except OSError as error:
        say(f"Error: {error}")
        return 1
    if host.startswith("linux") and not check_linux_clang():
        return 1
    preset = "windows" if host.startswith("windows") else "linux"
    say(f"Setup complete. Next: .tools/cmake/bin/cmake --workflow --preset {preset}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
