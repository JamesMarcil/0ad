#!/bin/sh

if [ "$1" = '--list' ]; then
	clang-tidy --list-checks -checks='*'
	exit
fi

if [ "$1" = '--fix' ]; then
	fix=-fix
	shift
fi

./libraries/source/premake-core/bin/premake5 --file=build/premake/premake5.lua --minimal-flags --cc-config=Release compilecommands

jq 'map(select(.file | (test(".*source/third_party.*") | not) and (test(".*generated.*") | not) and (test(".*test.*") | not) ))' compile_commands.json >compile_commands_filtered.json
mv compile_commands_filtered.json compile_commands.json

if [ $# -eq 0 ]; then
	run-clang-tidy -quiet -config-file=./source/tools/lint/clang-tidy/config.yml
else
	# shellcheck disable=SC2086
	IFS=',' run-clang-tidy -quiet ${fix} -checks="$*"
fi
