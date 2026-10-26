#!/usr/bin/env bash
# Create the OBNG Ghidra project from Oblivion.exe and run Ghidra's analysis
# once. Takes minutes. Re-running against an existing project fails on
# purpose: delete the project directory first if a fresh import is wanted.
#
#   tools/ghidra/import.sh [path to Oblivion.exe]
set -euo pipefail

exe="${1:-$HOME/.local/share/Steam/steamapps/common/Oblivion/Oblivion.exe}"
GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$HOME/tools/ghidra_12.1.4_PUBLIC}"
export JAVA_HOME="${JAVA_HOME:-$HOME/tools/jdk-21}"
export PATH="$JAVA_HOME/bin:$PATH"
projects="${OBNG_GHIDRA_PROJECTS:-$HOME/tools/ghidra-projects}"

expected="cdd2f0c5eff198d4f26b7b5b54ce4930"
actual="$(md5sum "$exe" | cut -d' ' -f1)"
if [ "$actual" != "$expected" ]; then
	echo "refusing: $exe has md5 $actual, OBNG targets 1.2.0.416 Steam GOTY ($expected)" >&2
	exit 2
fi
if [ -e "$projects/OBNG.gpr" ]; then
	echo "refusing: $projects/OBNG.gpr exists; delete it for a fresh import" >&2
	exit 2
fi
mkdir -p "$projects"
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless" "$projects" OBNG -import "$exe"
