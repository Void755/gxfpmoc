{pkgs ? import <nixpkgs> {}}:
pkgs.mkShell {
  buildInputs = [
    pkgs.gcc
    pkgs.gnumake
    pkgs.pkg-config
    pkgs.cmake
    pkgs.meson
    pkgs.ninja

    pkgs.glib
    pkgs.gusb
    pkgs.libusb1
    pkgs.libgudev
    pkgs.mbedtls
    pkgs.opencv4
    pkgs.doctest

    pkgs.clang-tools
    pkgs.bear
  ];

  shellHook = ''
    _gxfp_pc_dir="$PWD/.nix-pkgconfig"
    mkdir -p "$_gxfp_pc_dir"

    _gxfp_doctest_ver="$(${pkgs.gawk}/bin/awk '
      /^#define[ \t]+DOCTEST_VERSION_MAJOR[ \t]+/ {maj=$3}
      /^#define[ \t]+DOCTEST_VERSION_MINOR[ \t]+/ {min=$3}
      /^#define[ \t]+DOCTEST_VERSION_PATCH[ \t]+/ {pat=$3}
      END {
        if (maj != "" && min != "" && pat != "")
          printf "%s.%s.%s", maj, min, pat;
        else
          printf "2.0.0";
      }
    ' ${pkgs.doctest}/include/doctest/doctest.h)"

    cat >"$_gxfp_pc_dir/doctest.pc" <<EOF
    prefix=${pkgs.doctest}
    includedir=${pkgs.doctest}/include

    Name: doctest
    Description: Header-only C++ testing framework
    Version: $_gxfp_doctest_ver
    Cflags: -I${pkgs.doctest}/include
    EOF

    export PKG_CONFIG_PATH="$_gxfp_pc_dir''${PKG_CONFIG_PATH:+:}$PKG_CONFIG_PATH"
    export PKG_CONFIG_PATH_FOR_TARGET="$_gxfp_pc_dir''${PKG_CONFIG_PATH_FOR_TARGET:+:}$PKG_CONFIG_PATH_FOR_TARGET"
  '';
}
