{ pkgs, ... }:

{
  # Use a complete, consistent LLVM 21 toolchain.
  stdenv = pkgs.llvmPackages_21.stdenv;

  packages = with pkgs; [
    # Git and repository management
    git
    git-lfs
    github-cli

    # Node and PNPM added directly via standard system package attributes
    nodejs_24
    pnpm

    # Build tools
    llvmPackages_21.clang
    llvmPackages_21.clang-tools
    cmake
    ninja
    pkg-config

    # Debugging
    llvmPackages_21.lldb
    gdb

    # Pi Coding Agent - simple, light, customise for the project.
    pi-coding-agent

    # Bring worktrunk into the project.
    worktrunk

    # Clipboard
    wl-clipboard

    # Languages and utilities
    go
    curl
    ripgrep
    jq
    fd

    # Python tooling (version-agnostic CLIs; the interpreter comes from languages.python)
    uv
    ruff
    black
    mypy
    python314Packages.pytest
    python314Packages.ipython
    
    # YAML
    yq-go

    # Vulkan
    vulkan-loader
    vulkan-headers
    vulkan-validation-layers
    vulkan-tools       # Provides vulkaninfo, vkcube, etc.
    shaderc            # GLSL to SPIR-V compiler (glslc)
    mesa.drivers       # lavapipe: software Vulkan for scripts/smoke-test.sh --llvmpipe
    glfw               # Or SDL2, if your app uses it for windowing
    wayland
    wayland-protocols
    wayland-scanner
    libxkbcommon
    libffi

    # X11 development libraries: only for the clang-headless preset, which builds GLFW's
    # X11 backend so scripts/smoke-test.sh can render under a virtual display.
    xorg.libX11
    xorg.libXext
    xorg.libXrandr
    xorg.libXcursor
    xorg.libXinerama
    xorg.libXi
    xorg.xorgserver    # Xvfb, used when xvfb-run is not installed (e.g. NixOS)
    
  ];

  delta.enable = true;

  languages.cplusplus.enable = true;

  languages.python = {
    enable = true;
    package = pkgs.python314;
    venv.enable = true;
  };

  # Set environment variables so the Vulkan loader and layers are discoverable
  env = {
    VULKAN_SDK = "${pkgs.vulkan-headers}";
    VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
  };

  enterShell = ''
    # GLFW loads its window-system libraries at runtime, so the shell has to make them
    # findable, not just buildable: a Nix shell puts package libraries nowhere unless they
    # are named here. Wayland and libxkbcommon serve the default Wayland-only build; the
    # clang-headless preset builds GLFW X11-only (JETTISON_GLFW_X11=ON), and without the
    # xorg list below the smoke test dies at "X11: Failed to load Xlib".
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath ([ pkgs.wayland pkgs.libxkbcommon ] ++ (with pkgs.xorg; [ libX11 libXext libXrandr libXcursor libXinerama libXi libxcb libXau libXdmcp libpthreadstubs ]))}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    if [ -d .git ]; then
      echo "Checking Git LFS initialization..."
      git lfs install --local --force
    else
      echo "Not a Git repository. Skipping Git LFS setup."
    fi

    echo "  pnpm: $(pnpm --version)"

    echo "Vulkan development environment loaded!"
    vulkaninfo --summary | head -n 20 || echo "No Vulkan device found by the loader (headless CI: scripts/smoke-test.sh --llvmpipe pins the store ICD itself)."

    if [ ! -f ".config/wt.toml" ]; then
      echo "💡 Run 'wt config create --project' to link this repo."
    fi

    # Only inside a worktrunk session: outside one the command has nothing to copy and
    # a non-zero exit would abort shell entry, which CI relies on for devenv -c.
    if [ -n "''${WT_SESSION:-}" ]; then
      wt step copy-ignored
    fi
  '';
}