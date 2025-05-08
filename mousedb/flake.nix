{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      lib = nixpkgs.lib;
      nativeBuildInputs = pkgs: [
        pkgs.cmake
        pkgs.llvmPackages_20.clang-tools
        pkgs.llvmPackages_20.clang
        pkgs.ninja
        pkgs.pkg-config-unwrapped
        pkgs.unixtools.netstat
        pkgs.vcpkg
      ];
      env = pkgs: {
        VCPKG_ROOT = "${pkgs.vcpkg}/share/vcpkg";
        VCPKG_FORCE_SYSTEM_BINARIES = 1;
      };
    in
    {
      packages = lib.attrsets.genAttrs systems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          exe = pkgs.stdenvNoCC.mkDerivation {
            name = "mousedb-server";
            src = ./.;
            nativeBuildInputs = nativeBuildInputs pkgs ++ [
              pkgs.writableTmpDirAsHomeHook
              pkgs.cacert
            ];
            env = env pkgs;
            configurePhase = ''
              cd exe_server
              cmake --preset release
            '';
            buildPhase = ''
              cmake --build --preset release
            '';
            installPhase = ''
              mkdir -p $out/bin
              cp -r build/exe/* $out/bin/
            '';
          };
        }
      );
      devShells = lib.attrsets.genAttrs systems (
        system:
        let
          pkgs = import nixpkgs {
            inherit system;
          };
        in
        {
          default = pkgs.mkShellNoCC {
            nativeBuildInputs = nativeBuildInputs pkgs;
            buildInputs = [
              pkgs.vim
            ];
            env = env pkgs;
          };
        }
      );
      defaultPackage = lib.attrsets.genAttrs systems (system: self.packages.${system}.exe);
    };
}
