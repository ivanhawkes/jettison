# WAMR's ahead-of-time compiler, wamrc (docs/scripts.md): a script's .wasm compiled to an .aot the
# runtime loads in place of interpreting it. nixpkgs' wamr package builds only iwasm, so this builds
# wamr-compiler from the same release the engine's runtime is fetched at
# (source/external/CMakeLists.txt, GIT_TAG): an .aot file carries the format version of the wamrc
# that wrote it, and a runtime of another release may refuse it. Bump the two together.
#
# LLVM 18 is the release WAMR 2.4.5 builds and tests wamrc against (build-scripts/build_llvm.py,
# release/18.x); the engine's own toolchain stays at LLVM 21.
{
  lib,
  stdenv,
  fetchFromGitHub,
  cmake,
  ninja,
  llvmPackages_18,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "wamrc";
  version = "2.4.5";

  src = fetchFromGitHub {
    owner = "bytecodealliance";
    repo = "wasm-micro-runtime";
    tag = "WAMR-${finalAttrs.version}";
    hash = "sha256-HBew8kbi7AaKrAEZbPIlXqiO+1wDljSzEsE9+FWzxHE=";
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];

  buildInputs = [ llvmPackages_18.llvm ];

  # The whole tree stays writable, as WAMR's CMake scripts write to it, so the build is entered from
  # the compiler's directory rather than through sourceRoot.
  postPatch = ''
    cd wamr-compiler
  '';

  cmakeFlags = [
    (lib.cmakeBool "WAMR_BUILD_WITH_CUSTOM_LLVM" true)
    (lib.cmakeFeature "LLVM_DIR" "${lib.getDev llvmPackages_18.llvm}/lib/cmake/llvm")
  ];

  doInstallCheck = true;
  installCheckPhase = ''
    $out/bin/wamrc --version
  '';

  meta = {
    description = "WebAssembly Micro Runtime's ahead-of-time compiler";
    homepage = "https://github.com/bytecodealliance/wasm-micro-runtime";
    license = lib.licenses.asl20;
    mainProgram = "wamrc";
    platforms = lib.platforms.linux;
  };
})
