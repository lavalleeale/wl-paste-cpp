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
        dependencies = with pkgs; [
          wayland
          wayland-protocols
          wlr-protocols
          wayland-scanner
          meson
          nlohmann_json
          ninja
          pkg-config
        ];
      in {
        packages = {
          default = self.packages.${system}.wl-paste-cpp;
          wl-paste-cpp = pkgs.stdenv.mkDerivation {
            pname = "wl-paste-cpp";
            version = "0.2.0";

            src = ./.;

            nativeBuildInputs = [ pkgs.pkg-config pkgs.makeWrapper ];

            buildInputs = dependencies;

            configurePhase = ''
              meson setup -Dwrap_mode=nodownload build
            '';

            buildPhase = ''
              meson compile -C build
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp ./build/subprojects/copier/wl-copy-picker $out/bin/
              cp ./build/subprojects/watcher/wl-copy-slurp $out/bin/

              # Wrap the executable with proper environment variables
              wrapProgram $out/bin/wl-copy-picker \
                --prefix LD_LIBRARY_PATH : "${
                  pkgs.lib.makeLibraryPath dependencies
                }" \
                --prefix XDG_DATA_DIRS : "$XDG_ICON_DIRS:$GSETTINGS_SCHEMAS_PATH" \
                --set GSETTINGS_SCHEMAS_PATH "${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}"
              wrapProgram $out/bin/wl-copy-slurp \
                --prefix LD_LIBRARY_PATH : "${
                  pkgs.lib.makeLibraryPath dependencies
                }" \
                --prefix XDG_DATA_DIRS : "$XDG_ICON_DIRS:$GSETTINGS_SCHEMAS_PATH" \
                --set GSETTINGS_SCHEMAS_PATH "${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}"
            '';

            meta = {
              description = "Clipboard manager";
              platforms = pkgs.lib.platforms.linux;
            };
          };
        };

        devShells.default = pkgs.mkShell {
          nativeBuildInputs = [ pkgs.pkg-config pkgs.gdb ];
          buildInputs = dependencies;
        };
      });
}
