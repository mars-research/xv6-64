{
  description = "A simple project";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, ... }: let
    # System types to support.
    supportedSystems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
  in flake-utils.lib.eachSystem supportedSystems (system: let
    pkgs = nixpkgs.legacyPackages.${system};
    inherit (pkgs) lib;

    useCross = !pkgs.stdenv.hostPlatform.isx86;

    x86Pkgs =
      if useCross then pkgs.pkgsCross.x86_64-embedded # we only need to build bare-metal code
      else pkgs;

    gdb' = let
      # macOS: Don't build gdbserver - We only need to connect to QEMU's X86 gdbserver
      darwinGdb = (x86Pkgs.buildPackages.gdb.override {
        hostCpuOnly = true;
      }).overrideAttrs (old: let
        inherit (x86Pkgs.stdenv.cc) targetPrefix;
      in {
        configureFlags = (old.configureFlags or []) ++ [
	  (lib.enableFeature false "werror")
          "--enable-targets=${x86Pkgs.stdenv.targetPlatform.config}"
        ];
        postInstall = (old.postInstall or "") + ''
          if [[ -n "${targetPrefix}" ]]; then
            ln -s "$out/bin/${targetPrefix}gdb" "$out/bin/gdb"
          fi
        '';
      });
    in if pkgs.stdenv.isDarwin then darwinGdb else pkgs.gdb;
  in {
    devShell = x86Pkgs.mkShell {
      nativeBuildInputs = with pkgs; [
        qemu
        gdb'
      ];

      TOOLPREFIX = lib.optionalString useCross "x86_64-elf-";
    };
  });
}
