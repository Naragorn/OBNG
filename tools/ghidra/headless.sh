#!/usr/bin/env bash
# Run a Ghidra script against the OBNG project's Oblivion.exe without
# re-analysing it. The project is created once by tools/ghidra/import.sh.
#
#   tools/ghidra/headless.sh <Script.java> [script args...]
#
# Environment (defaults are the machine's install, see docs/re-workflow.md):
#   GHIDRA_INSTALL_DIR  Ghidra install          (default ~/tools/ghidra_12.1.4_PUBLIC)
#   JAVA_HOME           JDK 21                   (default ~/tools/jdk-21)
#   OBNG_GHIDRA_PROJECTS project directory       (default ~/tools/ghidra-projects)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$HOME/tools/ghidra_12.1.4_PUBLIC}"
export JAVA_HOME="${JAVA_HOME:-$HOME/tools/jdk-21}"
export PATH="$JAVA_HOME/bin:$PATH"
projects="${OBNG_GHIDRA_PROJECTS:-$HOME/tools/ghidra-projects}"

script="$1"; shift
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless" "$projects" OBNG \
	-process Oblivion.exe -noanalysis \
	-scriptPath "$here" \
	-postScript "$script" "$@"
