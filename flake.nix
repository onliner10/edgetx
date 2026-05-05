{
  description = "EdgeTX native development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
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
        in
        {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.git
              pkgs.ninja
              pkgs.pkg-config
              pkgs.uv
              python
            ];

            nativeBuildInputs = [
              llvm.clang
              llvm.libclang
            ];

            buildInputs = [
              pkgs.SDL2
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
              export LIBCLANG_PATH="${llvm.libclang.lib}/lib"
              export UV_PYTHON="${python}/bin/python3"
            '';
          };
        });
    };
}
