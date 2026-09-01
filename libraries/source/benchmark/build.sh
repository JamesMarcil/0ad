#!/bin/sh
set -e

cd "$(dirname "$0")"

SRCDIR=benchmark
LIB_VERSION=$(git -C "${SRCDIR}" describe --tags --always 2>/dev/null || echo unknown)

while [ "$#" -gt 0 ]; do
	case "$1" in
		--fetch-only)
			# Sources come from the "benchmark" git submodule; nothing to fetch here.
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

echo "Building Google Benchmark..."
if [ -e .already-built ] && [ "$(cat .already-built || true)" = "${LIB_VERSION}" ]; then
	echo "Skipping - already built (use --force-rebuild to override)"
	exit
else
	rm -f .already-built
fi

if [ ! -e "${SRCDIR}/CMakeLists.txt" ]; then
	echo "error: ${SRCDIR} submodule is not checked out." >&2
	echo "Run 'git submodule update --init -- libraries/source/benchmark/benchmark' and retry." >&2
	exit 1
fi

# configure
rm -Rf build
cmake -B build -S "${SRCDIR}" \
	-DBENCHMARK_ENABLE_TESTING=NO \
	-DBENCHMARK_ENABLE_GTEST_TESTS=NO \
	-DBENCHMARK_ENABLE_WERROR=NO \
	-DBENCHMARK_ENABLE_INSTALL=YES \
	-DBENCHMARK_ENABLE_DOXYGEN=NO \
	-DBENCHMARK_INSTALL_DOCS=NO \
	-DBENCHMARK_DOWNLOAD_DEPENDENCIES=NO \
	-DBENCHMARK_USE_BUNDLED_GTEST=NO \
	-DBUILD_SHARED_LIBS=NO \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_INSTALL_PREFIX="$(realpath .)"

# build
cmake --build build --config Release

# install
rm -Rf include lib share
cmake --install build --config Release

echo "${LIB_VERSION}" >.already-built
