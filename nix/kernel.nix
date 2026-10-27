{
  lib,
  stdenv,
  kernel,
}:
stdenv.mkDerivation {
  pname = "gxfp-kernel";
  version = "unstable";

  src = lib.sources.cleanSource ../kernel;

  nativeBuildInputs = kernel.moduleBuildDependencies;

  makeFlags = [
    "KDIR=${kernel.dev}/lib/modules/${kernel.modDirVersion}/build"
  ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib/modules/${kernel.modDirVersion}/extra
    install -m644 gxfp.ko \
      $out/lib/modules/${kernel.modDirVersion}/extra/gxfp.ko
    runHook postInstall
  '';

  dontStrip = true;

  passthru = {
    inherit (kernel) modDirVersion;
  };

  meta = {
    description = "GXFP5130 EC mailbox kernel module";
    license = lib.licenses.gpl2;
    platforms = lib.platforms.linux;
  };
}
