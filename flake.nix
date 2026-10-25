{
  description = "A basic rewrite of GNU coreutils for learning purposes.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      devShell = pkgs.mkShell {
        packages = with pkgs; [
          gcc
          libclang
          valgrind
          gnumake
          bash
        ];

        shellHook = ''
          export CC=gcc
        '';
      };
    in
    {
      devShells.${system}.default = devShell;
    };
}
