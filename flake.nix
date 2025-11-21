{
  description = "";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };
    # esp-dev = {
    #   url = "github:mirrexagon/nixpkgs-esp-dev";
    #   inputs.nixpkgs.follows = "nixpkgs";
    # };
  };

  outputs =
    inputs@{
      nixpkgs,
      flake-parts,
      # esp-dev,
      ...
    }:

    flake-parts.lib.mkFlake { inherit inputs; } (
      { self, ... }:
      {
        systems = [
          "x86_64-linux"
          "aarch64-linux"
          "aarch64-darwin"
        ];
        flake.overlays.default = nixpkgs.lib.composeManyExtensions [
          # inputs.esp-dev.overlays.default
        ];
        perSystem =
          {
            pkgs,
            system,
            ...
          }:
          {
            # 1. Configure pkgs to allow the insecure package
            _module.args.pkgs = import inputs.nixpkgs {
              inherit system;
              overlays = [ self.overlays.default ];
              config = {
                allowUnfree = true;
                permittedInsecurePackages = [
                  "python3.12-ecdsa-0.19.1"
                ];
              };
            };

            devShells.default = pkgs.mkShell {
              # We REMOVED inputsFrom = [ ... ]
              # This prevents IDF_PATH from being set to the Nix store path.

              buildInputs = with pkgs; [
                esphome
                python3
                git
                cmake
                ninja

                # Required for some ESPHome/PlatformIO operations
                libffi
                openssl
              ];

              shellHook = ''
                # Explicitly unset variables just in case
                unset IDF_PATH
                unset IDF_TOOLS_PATH

                # Standard Python path setup
                export PYTHONPATH=$PYTHONPATH:$(pwd)

                echo "---------------------------------------"
                echo "ESPHome Hybrid Environment"
                echo "Nix provides: esphome, cmake, ninja, python"
                echo "PlatformIO manages: ESP-IDF SDK & Compilers"
                echo "---------------------------------------"
              '';
            };
          };
      }
    );

}
