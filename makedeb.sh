#!/bin/bash -e

_pkgname=taxon
_pkgversion=$(git describe --tag | sed 's/^[^0-9]*//')
_pkgarch=$(dpkg --print-architecture)
_tempdir=$(readlink -f "./.makedeb")

rm -rf ${_tempdir}
mkdir -p ${_tempdir}
cp -pr DEBIAN -t ${_tempdir}

meson setup -Ddebug=true -Doptimization=3 build_makedeb
meson compile -Cbuild_makedeb
meson test -Cbuild_makedeb
DESTDIR=${_tempdir} meson install -Cbuild_makedeb

sed -i "s/{_pkgname}/${_pkgname}/" ${_tempdir}/DEBIAN/control
sed -i "s/{_pkgversion}/${_pkgversion}/" ${_tempdir}/DEBIAN/control
sed -i "s/{_pkgarch}/${_pkgarch}/" ${_tempdir}/DEBIAN/control

dpkg-deb --root-owner-group --build .makedeb "${_pkgname}_${_pkgversion}_${_pkgarch}.deb"
