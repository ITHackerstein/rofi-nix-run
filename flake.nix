{
    description = "rofi-nix-run";
    inputs = {
        nixpkgs.url = "nixpkgs";
        systems.url = "github:nix-systems/x86_64-linux";
        flake-utils = {
            url = "github:numtide/flake-utils";
            inputs.systems.follows = "systems";
        };
    };
    outputs = { self, nixpkgs, flake-utils, ... }: flake-utils.lib.eachDefaultSystem (system:
    let
        pkgs = nixpkgs.legacyPackages.${system};
        pname = "rofi-nix-run";
        version = "0.1.0";
        src = ./.;
        buildInputs = [
            pkgs.rofi-unwrapped
            pkgs.glib
            pkgs.cairo
            pkgs.yyjson
            pkgs.zenity
            pkgs.nix
        ];
        nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
        ];
    in
    {
        devShells.default = pkgs.mkShell {
            inherit buildInputs nativeBuildInputs;
        };

        packages.default = pkgs.stdenv.mkDerivation {
            inherit pname version src buildInputs nativeBuildInputs;

            postPatch = ''
                substituteInPlace src/constants.h \
                    --replace-warn '#define NIX_BINARY "nix"' '#define NIX_BINARY "${pkgs.lib.getExe pkgs.nix}"' \
                    --replace-warn '#define ZENITY_BINARY "zenity"' '#define ZENITY_BINARY "${pkgs.lib.getExe pkgs.zenity}"'
            '';

            meta = {
                description = "Simple Rofi plugin to launch Nix packages (especially GUI programs).";
                homepage = "https://github.com/ITHackerstein/rofi-nix-run";
                license = pkgs.lib.licenses.mit;
                platforms = pkgs.lib.platforms.linux;
            };
        };
    });
}
