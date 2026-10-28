{
  lib,
  stdenv,
  cmake,
  mbedtls,
}:
stdenv.mkDerivation {
  pname = "gxfp-tools";
  version = "unstable";

  src = lib.sources.cleanSource ../.;

  nativeBuildInputs = [ cmake ];
  buildInputs = [ mbedtls ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
  ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    install -m755 gxfp_psk_tool $out/bin/
    install -m755 gxfp_capture $out/bin/
    install -m755 gxfp_info $out/bin/
    runHook postInstall
  '';

  meta = {
    description = "Diagnostic and provisioning tools for GXFP5130";
    platforms = lib.platforms.linux;
  };
}
