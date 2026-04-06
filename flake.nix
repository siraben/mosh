{
  description = "Mosh (mobile shell) with improved Unicode support";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
      moshOverride = nixpkgs: final: prev: {
        mosh = prev.mosh.overrideAttrs (old: {
          version = "1.4.0-unicode-fix";

          src = builtins.path {
            path = ./.;
            name = "mosh-src";
            filter =
              path: type:
              let
                baseName = builtins.baseNameOf path;
              in
              baseName != ".git" && baseName != "result" && baseName != ".direnv";
          };

          # Keep the Nix-specific path substitution patches from nixpkgs,
          # but drop the protobuf3 fetchpatch (already in our source tree).
          patches = [
            (nixpkgs + "/pkgs/by-name/mo/mosh/ssh_path.patch")
            (nixpkgs + "/pkgs/by-name/mo/mosh/mosh-client_path.patch")
            (nixpkgs + "/pkgs/by-name/mo/mosh/bash_completion_datadir.patch")
          ];
        });
      };
    in
    {
      overlays.default = moshOverride nixpkgs;

      packages = forAllSystems (pkgs: {
        default = (pkgs.extend self.overlays.default).mosh;
        mosh = self.packages.${pkgs.system}.default;
      });
    };
}
