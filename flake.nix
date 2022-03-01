{
  description = "My version of Watchy";

  outputs = { self, nixpkgs }:
  let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
  in
  {
    devShell.${system} = pkgs.mkShell {
      buildInputs = with pkgs; [
        arduino
        arduino-cli
        clang
      ];
    };
  };
}
