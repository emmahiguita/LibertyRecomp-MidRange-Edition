#!/usr/bin/env python3
"""Run the pinned SDK compiler with Unix-safe arguments and dependencies.

CMake's VERBATIM escaping does not quote braces on Unix. macOS /bin/sh expands
-DOPTION={0,1} into two definitions before FidelityFX_SC can parse permutations.
The generated command uses inert tokens; decode only after the shell has exited.
The Windows compiler writes absolute Wine drive paths in its GCC depfile. Convert
those paths before CMake processes them, otherwise Ninja permanently records
nonexistent relative paths such as sdk/src/backends/vk/Z:/Users/....
"""
import os
from pathlib import Path
import re
import subprocess
import sys


def normalize_depfile(depfile: Path, target: Path) -> None:
    text = depfile.read_text(encoding="utf-8")
    # DumpDepfileGCC writes unescaped, absolute Windows paths. Splitting at
    # drive prefixes also preserves spaces inside source/build directory names.
    starts = list(re.finditer(r"(?<!\S)([A-Za-z]):[/\\]", text))
    if not starts:
        return
    prefix = Path(os.environ.get("WINEPREFIX", Path.home() / ".wine"))
    roots = {}
    dependencies = []
    for index, match in enumerate(starts):
        if index == 0:
            continue  # Use the authoritative -output/-name target below.
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        value = text[match.start():end].strip().replace("\\", "/")
        drive = match.group(1).lower()
        if drive not in roots:
            roots[drive] = (prefix / "dosdevices" / f"{drive}:").resolve(strict=True)
        dependencies.append(str(roots[drive] / value[3:]))

    def escape(value: str) -> str:
        return (value.replace("\\", "\\\\").replace("$", "$$")
                .replace("#", "\\#").replace(" ", "\\ ").replace(":", "\\:"))

    normalized = escape(str(target.absolute())) + ":"
    if dependencies:
        normalized += " " + " ".join(escape(value) for value in sorted(set(dependencies)))
    normalized += "\n"
    if text != normalized:
        depfile.write_text(normalized, encoding="utf-8")


def main(arguments: list[str]) -> int:
    command = [argument.replace("@FFX_OPEN@", "{").replace("@FFX_CLOSE@", "}")
               for argument in arguments]
    if not command:
        raise SystemExit("expected FidelityFX compiler command")
    result = subprocess.run(command, check=False)
    if result.returncode:
        return result.returncode
    output = next((value.removeprefix("-output=") for value in command
                   if value.startswith("-output=")), None)
    name = next((value.removeprefix("-name=") for value in command
                 if value.startswith("-name=")), None)
    if "-deps=gcc" in command:
        if not output or not name:
            raise SystemExit("GCC dependencies require FidelityFX -output and -name")
        target = Path(output) / f"{name}_permutations.h"
        normalize_depfile(Path(str(target) + ".d"), target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
