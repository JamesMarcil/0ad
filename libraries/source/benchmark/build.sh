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
	-G "Visual Studio 18 2026" -A x64 -T v143 \
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
	-DCMAKE_INSTALL_PREFIX="$(realpath .)" \
	-DBENCHMARK_CXX_STANDARD=20

# build
cmake --build build --config Release

# install
rm -Rf include lib share
cmake --install build --config Release

# Also build Debug variant if on Windows
if [ "$(uname -s)" = "MINGW64_NT" ] || [ "$(uname -s)" = "MSYS_NT" ] || [ -f /c/Windows/System32/cmd.exe ]; then
	echo "Building Debug variant..."
	rm -Rf build-debug
	cmake -B build-debug -S "${SRCDIR}" \
		-G "Visual Studio 18 2026" -A x64 -T v143 \
		-DBENCHMARK_ENABLE_TESTING=NO \
		-DBENCHMARK_ENABLE_GTEST_TESTS=NO \
		-DBENCHMARK_ENABLE_WERROR=NO \
		-DBENCHMARK_ENABLE_INSTALL=YES \
		-DBENCHMARK_ENABLE_DOXYGEN=NO \
		-DBENCHMARK_INSTALL_DOCS=NO \
		-DBENCHMARK_DOWNLOAD_DEPENDENCIES=NO \
		-DBENCHMARK_USE_BUNDLED_GTEST=NO \
		-DBUILD_SHARED_LIBS=NO \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_INSTALL_LIBDIR=lib-debug \
		-DCMAKE_INSTALL_PREFIX="$(realpath .)" \
		-DBENCHMARK_CXX_STANDARD=20

	cmake --build build-debug --config Debug
	cmake --install build-debug --config Debug

	# Rename the debug lib to match convention (name + "d" suffix)
	if [ -f lib-debug/benchmark.lib ]; then
		mv lib-debug/benchmark.lib lib/benchmarkd.lib
		mv lib-debug/benchmark_main.lib lib/benchmark_maind.lib 2>/dev/null || true
		rm -Rf lib-debug
	fi
fi

echo "${LIB_VERSION}" >.already-built
