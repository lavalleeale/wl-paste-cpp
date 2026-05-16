{
  description = "Clipboard manager";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        buildDependencies = with pkgs; [ wayland nlohmann_json ];
        nativeDependencies = with pkgs; [
          cmake
          meson
          ninja
          pkg-config
          wayland-protocols
          wayland-scanner
          wlr-protocols
        ];
      in {
        packages = {
          default = self.packages.${system}.wl-paste-cpp;
          wl-paste-cpp = pkgs.stdenv.mkDerivation {
            pname = "wl-paste-cpp";
            version = "0.2.0";

            src = ./.;

            nativeBuildInputs = nativeDependencies;

            buildInputs = buildDependencies;

            configurePhase = ''
              meson setup -Dwrap_mode=nodownload build
            '';

            buildPhase = ''
              meson compile -C build
            '';

            checkPhase = ''
              meson test -C build --print-errorlogs
            '';

            doCheck = true;

            installPhase = ''
              mkdir -p $out/bin
              cp ./build/subprojects/copier/wl-copy-picker $out/bin/
              cp ./build/subprojects/watcher/wl-copy-slurp $out/bin/
            '';

            meta = {
              description = "Clipboard manager";
              platforms = pkgs.lib.platforms.linux;
            };
          };
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = nativeDependencies ++ [ pkgs.gdb ];
          buildInputs = buildDependencies;
        };
      });
}
