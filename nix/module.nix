self: {
  lib,
  config,
  pkgs,
  ...
}: let
  cfg = config.hardware.gxfp;
  system = pkgs.stdenv.hostPlatform.system;
in {
  options.hardware.gxfp = {
    enable = lib.mkEnableOption "GXFP5130 eSPI fingerprint sensor support";
  };

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [
      (config.boot.kernelPackages.callPackage ./kernel.nix {})
    ];
    boot.kernelModules = ["gxfp"];

    services.udev.extraRules = ''
      SUBSYSTEM=="misc", KERNEL=="gxfp", MODE="0660", TAG+="uaccess"
    '';

    services.fprintd = {
      enable = true;
      package = lib.mkForce self.packages.${system}.fprintd-gxfp;
    };

    systemd.services.fprintd.serviceConfig.DeviceAllow = lib.mkAfter [
      "/dev/gxfp rw"
    ];

    environment.systemPackages = [
      self.packages.${system}.gxfp-tools
    ];
  };
}
