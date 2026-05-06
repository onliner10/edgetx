{
  description = "EdgeTX native development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.nixpkgs-25_05.url = "github:NixOS/nixpkgs/nixos-25.05";
  inputs.serena.url = "github:oraios/serena";
  inputs.serena.inputs.nixpkgs.follows = "nixpkgs";

  outputs = { self, nixpkgs, nixpkgs-25_05, serena }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          armPkgs = import nixpkgs-25_05 { inherit system; };
          llvm = pkgs.llvmPackages;
          python = pkgs.python312.withPackages (ps: [
            ps.libclang
            ps.jinja2
            ps.lz4
            ps.pillow
            ps.pydantic
            ps.pyelftools
            ps."typing-extensions"
          ]);
          sdl3 = pkgs.sdl3;
          edge16Clangd = pkgs.writeShellScriptBin "edge16-clangd" ''
            exec ${llvm.clang-unwrapped}/bin/clangd \
              --query-driver="${armPkgs.gcc-arm-embedded}/bin/arm-none-eabi-*" \
              "$@"
          '';
        in
        {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              armPkgs.gcc-arm-embedded
              pkgs.git
              pkgs.ninja
              pkgs.pkg-config
              pkgs.uv
              serena.packages.${system}.serena
              python
              edge16Clangd
            ];

            nativeBuildInputs = [
              llvm.clang
              llvm.libclang
            ];

            buildInputs = [
              pkgs.SDL2
              sdl3
              pkgs.libx11
              pkgs.libxcursor
              pkgs.libxext
              pkgs.libxi
              pkgs.libxinerama
              pkgs.libxrandr
              pkgs.xorgproto
            ];

            shellHook = ''
              export CMAKE_GENERATOR=Ninja
              export LD_LIBRARY_PATH="${sdl3}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
              export LIBCLANG_PATH="${llvm.libclang.lib}/lib"
              export UV_PYTHON="${python}/bin/python3"
            '';
          };
        });
    };
}
