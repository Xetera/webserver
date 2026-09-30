{
  pkgs,
  lib,
  config,
  inputs,
  ...
}:

{
  # https://devenv.sh/basics/
  env.GREET = "devenv";
  env.LLDB_DEBUGSERVER_PATH = "/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/debugserver";
  # https://devenv.sh/packages/
  packages = [
    pkgs.llvmPackages_latest.clang
  ];
}
