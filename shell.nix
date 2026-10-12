{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];
  buildInputs = with pkgs; [
    qt6.qtbase
    ffmpeg
    opencv
  ];
}
