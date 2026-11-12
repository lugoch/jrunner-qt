#!/usr/bin/env bash
# 生成源码 tarball 并构建 Arch pacman 包(产物在 packaging/arch/)。
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(cat VERSION)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/jrunner-$version"
cp -r qt CMakeLists.txt README.md LICENSE VERSION "$stage/jrunner-$version/"
tar czf "packaging/arch/jrunner-$version.tar.gz" -C "$stage" "jrunner-$version"

cd packaging/arch
updpkgsums
makepkg -f