{
  description = "GXFP5130 eSPI fingerprint sensor stack";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    supportedSystems = ["x86_64-linux" "aarch64-linux"];
    forEachSystem = f:
      nixpkgs.lib.genAttrs supportedSystems (system:
        f (import nixpkgs {inherit system;}));
  in {
    overlays.default = final: _prev: {
      gxfp-kernel = final.linuxPackages_latest.callPackage ./nix/kernel.nix {};
      gxfp-tools = final.callPackage ./nix/tools.nix {};
      libfprint-gxfp = final.callPackage ./nix/libfprint.nix {};
      fprintd-gxfp = final.callPackage ./nix/fprintd.nix {
        libfprint-gxfp = final.callPackage ./nix/libfprint.nix {};
      };
    };

    packages = forEachSystem (pkgs: let
      libfprint-gxfp = pkgs.callPackage ./nix/libfprint.nix {};
    in rec {
      gxfp-kernel = pkgs.linuxPackages_latest.callPackage ./nix/kernel.nix {};
      gxfp-tools = pkgs.callPackage ./nix/tools.nix {};
      inherit libfprint-gxfp;
      fprintd-gxfp = pkgs.callPackage ./nix/fprintd.nix {
        inherit libfprint-gxfp;
      };
      default = fprintd-gxfp;
    });

    devShells = forEachSystem (pkgs: {
      default = import ./shell.nix {inherit pkgs;};
      kernel = import ./kernel/shell.nix {inherit pkgs;};
    });

    nixosModules = rec {
      default = import ./nix/module.nix self;
      gxfp = default;
    };
  };
}
