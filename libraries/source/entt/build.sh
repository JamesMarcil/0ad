#!/bin/sh
set -e

cd "$(dirname "$0")"

SRCDIR=entt
LIB_VERSION=$(git -C "${SRCDIR}" describe --tags --always 2>/dev/null || echo unknown)

while [ "$#" -gt 0 ]; do
	case "$1" in
		--fetch-only)
			# Sources come from the "entt" git submodule; nothing to fetch here.
			exit
			;;
		--force-rebuild) rm -f .already-built ;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
	shift
done

echo "Installing EnTT headers..."
if [ -e .already-built ] && [ "$(cat .already-built || true)" = "${LIB_VERSION}" ]; then
	echo "Skipping - already installed (use --force-rebuild to override)"
	exit
else
	rm -f .already-built
fi

if [ ! -e "${SRCDIR}/src/entt/entt.hpp" ]; then
	echo "error: ${SRCDIR} submodule is not checked out." >&2
	echo "Run 'git submodule update --init -- libraries/source/entt/entt' and retry." >&2
	exit 1
fi

# EnTT is header-only; there's nothing to compile, just install the
# headers into the standard "include" directory used by our other
# bundled source libs (see add_source_include_paths() in
# build/premake/extern_libs5.lua).
rm -Rf include
mkdir -p include
cp -R "${SRCDIR}/src/entt" include/entt

echo "${LIB_VERSION}" >.already-built
