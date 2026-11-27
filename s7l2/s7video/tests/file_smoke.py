# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

from pathlib import Path
import subprocess
import sys
import tempfile


recorder, graph, plugin, roundtrip, replay = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    recording = work / "recording.mjpeg"
    command = (recorder, plugin, roundtrip, "encode", "5", recording.name)
    subprocess.run(command, cwd=work, check=True)
    original = recording.read_bytes()
    if subprocess.run(command, cwd=work).returncode != 1 or recording.read_bytes() != original:
        raise RuntimeError("Existing recording was not protected")
    failed = subprocess.run(
        (recorder, plugin, replay, "file", "6", "incomplete.mjpeg"), cwd=work
    )
    if failed.returncode != 1 or list(work.glob("incomplete.mjpeg*")):
        raise RuntimeError("Failed recording left a destination or staging directory")
    (work / "truncated.mjpeg").write_bytes(original[:-1])
    subprocess.run((graph, plugin, roundtrip, replay, "truncated.mjpeg"), cwd=work, check=True)
