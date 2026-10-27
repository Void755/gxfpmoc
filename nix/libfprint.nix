{
  lib,
  stdenv,
  pkg-config,
  meson,
  python3,
  ninja,
  cmake,
  gusb,
  pixman,
  glib,
  gobject-introspection,
  cairo,
  libgudev,
  udevCheckHook,
  gtk-doc,
  docbook-xsl-nons,
  docbook_xml_dtd_43,
  openssl,
  mbedtls,
  opencv4,
  doctest,
  nss,
}:
stdenv.mkDerivation {
  pname = "libfprint-gxfp";
  version = "unstable";
  outputs = ["out" "devdoc"];

  src = lib.sources.cleanSource ../.;

  postPatch = ''
    patchShebangs \
      libfprint/tests/unittest_inspector.py \
      libfprint/tests/virtual-image.py \
      libfprint/tests/umockdev-test.py \
      libfprint/tests/test-generated-hwdb.sh
  '';

  preConfigure = ''
    cd libfprint
  '';

  nativeBuildInputs = [
    pkg-config
    meson
    ninja
    cmake
    gtk-doc
    docbook-xsl-nons
    docbook_xml_dtd_43
    gobject-introspection
    udevCheckHook
  ];

  buildInputs = [
    gusb
    pixman
    glib
    cairo
    libgudev
    openssl
    mbedtls
    opencv4
    doctest
    nss
  ];

  mesonFlags = [
    "-Dudev_rules_dir=${placeholder "out"}/lib/udev/rules.d"
    "-Ddrivers=gxfp"
    "-Dudev_hwdb_dir=${placeholder "out"}/lib/udev/hwdb.d"
  ];

  postInstall = ''
    install -Dm644 "$src/libfprint/libfprint/sigfm/sigfm.hpp" \
      "$out/include/libfprint-2/sigfm/sigfm.hpp"
  '';

  nativeInstallCheckInputs = [
    (python3.withPackages (p: with p; [pygobject3]))
  ];

  doCheck = false;
  doInstallCheck = true;
  installCheckPhase = ''
    runHook preInstallCheck
    runHook postInstallCheck
  '';

  meta = {
    homepage = "https://fprint.freedesktop.org/";
    description = "libfprint fork with GXFP5130 eSPI support";
    license = lib.licenses.lgpl21Only;
    platforms = lib.platforms.linux;
  };
}
