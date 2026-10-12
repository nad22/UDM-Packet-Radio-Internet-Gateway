#!/usr/bin/env python3
"""Choose the next firmware release version and prepare OTA metadata."""

import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")


def version_tuple(value: str) -> tuple[int, int, int] | None:
    match = VERSION_PATTERN.fullmatch(value.strip())
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def git_output(*args: str) -> str:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.strip()


def main() -> None:
    header_path = ROOT / "include" / "firmware_version.h"
    tags = git_output("tag", "--list", "v[0-9]*").splitlines()
    tagged_versions = [
        (parsed, tag)
        for tag in tags
        if (parsed := version_tuple(tag)) is not None
    ]
    head = git_output("rev-parse", "HEAD")

    # Reuse a tag already created for this commit if the workflow is re-run.
    matching_head_versions = []
    for parsed, tag in tagged_versions:
        tag_commit = git_output("rev-parse", f"{tag}^{{commit}}")
        if tag_commit == head:
            matching_head_versions.append(parsed)

    if matching_head_versions:
        version = max(matching_head_versions)
    else:
        if tagged_versions:
            previous = max(parsed for parsed, _ in tagged_versions)
        else:
            header = header_path.read_text(encoding="utf-8")
            match = re.search(r'#define FIRMWARE_VERSION "([^"]+)"', header)
            previous = version_tuple(match.group(1)) if match else None
            if previous is None:
                raise RuntimeError("No valid existing firmware version was found")
        version = (previous[0], previous[1], previous[2] + 1)

    version_text = ".".join(str(part) for part in version)
    header_path.write_text(
        "#pragma once\n\n#define FIRMWARE_VERSION \"" + version_text + "\"\n",
        encoding="utf-8",
    )
    (ROOT / "ota" / "version.txt").write_text(version_text + "\n", encoding="utf-8")

    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"version={version_text}\n")
    print(f"Prepared firmware release v{version_text}")


if __name__ == "__main__":
    main()
