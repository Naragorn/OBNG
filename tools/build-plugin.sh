#!/usr/bin/env bash
# Builds OBNG.dll on Linux with clang-cl and lld-link against xwin's
# winsysroot (plugin/cmake/toolchain-linux-clang-cl.cmake), after running the
# plugin's and the engine's unit tests natively (clang++ -m32), and deploys
# the DLL and the INI into the Linux game's Data/OBSE/Plugins when the game is
# not running. Nothing is deployed when a test or the build fails.
#
#   tools/build-plugin.sh              tests, DLL, deploy
#   tools/build-plugin.sh --no-deploy
#
# OBNG_GAME_DIR points somewhere other than Steam's default Oblivion folder.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
game="${OBNG_GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Oblivion}"
deploy=1
[[ "${1:-}" == "--no-deploy" ]] && deploy=0

game_running() {
	"$root/tools/game-running.sh"
}

echo "== engine tests"
cmake -S "$root/tests" -B "$root/build-tests" -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 >/dev/null
cmake --build "$root/build-tests" >/dev/null
ctest --test-dir "$root/build-tests" --output-on-failure | tail -3

echo "== plugin tests"
cmake -S "$root/plugin/tests" -B "$root/build-plugin-tests" -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-m32 >/dev/null
cmake --build "$root/build-plugin-tests" >/dev/null
ctest --test-dir "$root/build-plugin-tests" --output-on-failure | tail -3

echo "== OBNG.dll"
cmake -S "$root/plugin" -B "$root/build-plugin" -G Ninja -DCMAKE_BUILD_TYPE=Release \
	--toolchain "$root/plugin/cmake/toolchain-linux-clang-cl.cmake" >/dev/null
cmake --build "$root/build-plugin"
ls -la "$root/build-plugin/OBNG.dll"
sha256sum "$root/build-plugin/OBNG.dll" | cut -c1-16

if [[ "$deploy" == 1 ]]; then
	target="$game/Data/OBSE/Plugins"
	if [[ ! -d "$target" ]]; then
		echo "no $target - not deployed" >&2
		exit 2
	fi
	if game_running; then
		echo "Oblivion is running - not deployed" >&2
		exit 3
	fi
	cp "$root/build-plugin/OBNG.dll" "$target/OBNG.dll"
	[[ -f "$target/OBNG.ini" ]] || cp "$root/plugin/OBNG.ini" "$target/OBNG.ini"
	echo "Deployed OBNG.dll to $target (OBNG.ini kept if it was there)"
fi
