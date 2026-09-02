#!/bin/sh
set -e

cd "$(dirname "$0")"

SRCDIR=entt

while [ "$#" -gt 0 ]; do
	case "$1" in
		--fetch-only)
			# Sources come from the "entt" git submodule; nothing to fetch here.
			exit
			;;
		--force-rebuild)
			# No-op; entt is header-only with no build artifacts
			;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
	shift
done

echo "Checking EnTT headers..."
if [ ! -e "${SRCDIR}/src/entt/entt.hpp" ]; then
	echo "error: ${SRCDIR} submodule is not checked out." >&2
	echo "Run 'git submodule update --init -- libraries/source/entt/entt' and retry." >&2
	exit 1
fi

echo "EnTT headers verified (header-only library, no install step needed)"
