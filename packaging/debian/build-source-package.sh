#!/usr/bin/env bash
# Build a Debian source package suitable for Launchpad PPA upload.
#
# Usage: ./packaging/debian/build-source-package.sh [output_dir]
#
# This script:
#   1. Creates a clean source tree from git archive
#   2. Copies the debian/ packaging into it
#   3. Runs dpkg-buildpackage -S to produce the .dsc + .tar.xz
#
# Prerequisites: dpkg-dev, debhelper

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SPEC="$REPO_ROOT/packaging/rpm/mupager.spec"

VERSION=$(grep '^Version:' "$SPEC" | awk '{print $2}')
PACKAGE="mupager"
OUTDIR="${1:-$REPO_ROOT/build/debian}"

echo "==> Building source package for $PACKAGE $VERSION"

# Clean and create work directory
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

SRC="$WORKDIR/${PACKAGE}-${VERSION}"
mkdir -p "$SRC"

# Export clean source from git
git -C "$REPO_ROOT" archive HEAD | tar -x -C "$SRC"

# Copy debian packaging
cp -a "$SCRIPT_DIR" "$SRC/debian"
# Remove the build script itself from the package
rm -f "$SRC/debian/build-source-package.sh"

# Build source package
cd "$SRC"
dpkg-buildpackage -S -d -us -uc

# Collect results
mkdir -p "$OUTDIR"
cp "$WORKDIR"/${PACKAGE}_${VERSION}* "$OUTDIR"/
echo "==> Source package written to $OUTDIR"
ls -la "$OUTDIR"/${PACKAGE}_${VERSION}*
