{
  description = "PlantOS";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.05";
    flake-parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };
    taskfile-parts.url = "github:tobjaw/taskfile-parts";
  };

  outputs =
    inputs@{
      nixpkgs,
      flake-parts,
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
        imports = [
          inputs.taskfile-parts.flakeModules.default
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
              config = {
                permittedInsecurePackages = [
                  "python3.12-ecdsa-0.19.1"
                ];
              };
            };

            taskfile = {
              enable = true;
              path = ./Taskfile.yml;
              shell = {
                buildInputs = with pkgs; [
                  esphome
                  python3
                  pipx
                  git
                  cmake
                  ninja
                  libffi
                  openssl
                ];

                shellHook = ''
                  echo
                  echo '                                    =%         '
                  echo '============================  @@   @=%@   @@   '
                  echo ' ##   ##  #######    #####     +@@ @+%@ @=%    '
                  echo '##   ##  ##    ##  ##   ##     @=#%.=% =+#@    '
                  echo '#######      ###   ##   ##       =%#=@=%%      '
                  echo '     ##    ###     ##   ##    @+++%%%%#%=%%@   '
                  echo '     ##  ########   #####        @#%=@%%@      '
                  echo '============================        +          '

                  # Install esp-idf-monitor for IDFMON mode (decoded backtraces)
                  if ! command -v idf-monitor &>/dev/null; then
                    echo "Installing esp-idf-monitor via pipx..."
                    pipx install esp-idf-monitor --quiet 2>/dev/null || pipx install esp-idf-monitor
                  fi
                  # Ensure pipx binaries are in PATH
                  export PATH="$HOME/.local/bin:$PATH"
                '';
              };
            };

          };
      }
    );

}
