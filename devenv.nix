{ pkgs, config, ... }:

{
  # Enable dotenv support for loading environment variables from a .env file.
  dotenv.enable = true;

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
    llvmPackages_21.lld # wasm-ld, which links scripts (phase 11b): the script suite's and the sample mods'
    rustc # Scripts in Rust (phase 11b): its wasm32-unknown-unknown library comes with it, linked by wasm-ld
    (callPackage ./nix/wamrc.nix { }) # WAMR's ahead-of-time compiler, at the engine's WAMR release
    cmake
    ninja
    pkg-config
    ccache # Compiler cache: CMake compiles through it when it is on the PATH (JETTISON_CCACHE)
    shellcheck
    lychee # Markdown link checker: lychee --offline --include-fragments '*.md' 'docs/**/*.md'
    mermaid-cli # Mermaid diagrams in the docs checked: mmdc -i diagram.mmd -o diagram.svg fails on a bad one

    # Debugging and profiling
    llvmPackages_21.lldb
    llvmPackages_21.llvm # llvm-symbolizer: the sanitisers and libFuzzer name a crash's frames with it
    gdb
    perf # Sampling profiler: perf record -e task-clock samples user space at perf_event_paranoid 2
    hotspot # perf's data browsed: flame graphs, callers and callees
    heaptrack # Allocations: what allocates, how often and how much
    valgrind # Callgrind counts instructions, so two commits compare without wall-clock noise
    kdePackages.kcachegrind # Callgrind's data browsed
    tracy # The profiler for the client source/external pins: the two must be the same release

    # Go programming language.
    go

    # Useful command-line utilities for searching, filtering, and viewing files.
    ripgrep
    fd
    fzf
    bat
    eza
    tree
    jq
    yq-go

    # Retrieve files from the internet.
    wget
    curl

    # Python tooling (version-agnostic CLIs; the interpreter comes from languages.python)
    uv
    ruff
    black
    mypy

    # Vulkan
    vulkan-loader
    vulkan-headers
    vulkan-validation-layers
    vulkan-tools # Provides vulkaninfo, vkcube, etc.
    shaderc # GLSL to SPIR-V compiler (glslc)
    mesa # lavapipe: software Vulkan for scripts/smoke-test.sh --llvmpipe
    glfw # Or SDL2, if your app uses it for windowing
    wayland
    wayland-protocols
    wayland-scanner
    libxkbcommon
    libffi

    # X11 development libraries: only for the clang-headless preset, which builds GLFW's
    # X11 backend so scripts/smoke-test.sh can render under a virtual display.
    libX11
    libXext
    libXrandr
    libXcursor
    libXinerama
    libXi
    xorgserver # Xvfb for headless smoke tests

    # KTX tools for handling KTX texture files, and ImageMagick for converting source images
    # before they are encoded (scripts/import-planet-textures.sh).
    ktx-tools
    imagemagick
  ];

  delta.enable = true;

  languages.cplusplus.enable = true;

  languages.python = {
    enable = true;
    package = pkgs.python314.withPackages (ps: [
      ps.numpy
      ps.pillow
      ps.pytest
      ps.ipython
    ]);
  };
    
  # Set environment variables so the Vulkan loader and layers are discoverable
  env = {
    VULKAN_SDK = "${pkgs.vulkan-headers}";
    VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
    JETTISON_LAVAPIPE_ICD = "${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.x86_64.json";

    # ccache rewrites absolute paths under its base directory to relative ones before it hashes, so
    # a worktree beside this one (worktrunk's ../jettison.<branch>) finds what this one compiled.
    # The directory is the parent of the checkout, where the worktrees are. Debug-info builds still
    # hash their build directory (ccache's hash_dir), which keeps every object's recorded source
    # paths true to the tree it is in; those share within a tree, Release across them too. Four
    # trees' objects are 1.2 GB, compressed to about half: 10 GB holds a good many branches.
    CCACHE_BASEDIR = builtins.dirOf config.devenv.root;
    CCACHE_MAXSIZE = "10G";
  };

  enterShell = ''
    # GLFW loads its window-system libraries at runtime, so the shell has to make them
    # findable, not just buildable: a Nix shell puts package libraries nowhere unless they
    # are named here. Wayland and libxkbcommon serve the default Wayland-only build; the
    # clang-headless preset builds GLFW X11-only (JETTISON_GLFW_X11=ON), and without the
    # xorg list below the smoke test dies at "X11: Failed to load Xlib". miniaudio loads its
    # sound libraries at runtime too: without libpulse and libasound here it falls through to
    # the system's PipeWire JACK library, whose clients leak at exit (34 allocations each).
    export LD_LIBRARY_PATH="${
      pkgs.lib.makeLibraryPath (
        [
          pkgs.wayland
          pkgs.libxkbcommon
          pkgs.gcc.cc.lib
          pkgs.libpulseaudio
          pkgs.alsa-lib
        ]
        ++ (with pkgs; [
          libX11
          libXext
          libXrandr
          libXcursor
          libXinerama
          libXi
          libxcb
          libXau
          libXdmcp
          libpthreadstubs
        ])
      )
    }''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    if [ -d .git ]; then
      echo "Checking Git LFS initialization..."
      git lfs install --local --force
    else
      echo "Not a Git repository. Skipping Git LFS setup."
    fi

    echo "  pnpm: $(pnpm --version)"

    echo "Vulkan development environment loaded!"

    # Correctly register Context7 MCP via local stdio transport
    if [ -n "''${CONTEXT7_API_KEY:-}" ]; then
      echo "🔄 Registering Context7 MCP Server (stdio mode) in Pi Agent..."
      pi mcp add context7 --exposure direct \
        --env "CONTEXT7_API_KEY=$CONTEXT7_API_KEY" \
        -- npx -y @upstash/context7-mcp 2>/dev/null || true
    else
      echo "⚠️ Warning: CONTEXT7_API_KEY is not set. Check your .env file."
    fi

    ktx --version

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
