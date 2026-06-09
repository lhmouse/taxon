#!/bin/bash -e

meson setup  \
  -Ddebug=true -Doptimization=3  \
  --prefix="/usr/local"  \
  build_makedeb

meson compile -C build_makedeb
meson test -C build_makedeb

echo 'Type Annotation eXtension for JSON' >description-pak

sudo checkinstall  \
  --pkgname="taxon-local"  \
  --provides="libtaxon-dev"  \
  --pkgversion="$(git describe --tags | sed 's/^[^0-9]*//')"  \
  --pkgsource="https://github.com/lhmouse/taxon"  \
  --pkglicense="BSD-3-Clause"  \
  --pkggroup="devel"  \
  --pkgarch="$(dpkg --print-architecture)"  \
  --nodoc --backup=no --default --fstrans=no --install=yes --deldesc  \
  meson install -C build_makedeb
