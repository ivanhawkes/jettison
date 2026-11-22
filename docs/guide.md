# Working with Jettison

A guide for someone new to the engine: how to do the common things, step by step, with the commands
to run. It shows the way through; the reasons and the contracts are in
[architecture](architecture.md) and [build and gates](build-and-gates.md), which each section links.

## Making and releasing a mod

A mod is a directory of files mounted over the game's content: while you write it you work in the
directory, and you give players one `.pack` file made from it. The engine treats `mymod/` and
`mymod.pack` as the same mod, identified as `mod:mymod`, so what you test unpacked is what players
get packed. How mounting, packs and layering work is in
[files, packs and mods](architecture.md#files-packs-and-mods).

### Lay out the directory

Paths in the mod are the game's virtual paths, each kind of file in its place:

```
mymod/
  mod.yaml                      # describes the mod; optional, but a release should have one
  defs/goods/beer.patch.yaml    # a patch: changes the fields it names, keeps the rest
  defs/goods/cider.yaml         # a whole file: adds a definition, or replaces one entirely
  defs/scripts/mymod.yaml       # declares a script, if the mod has one
  scripts/mymod.wasm            # the script's compiled module
  ...                           # any other file replaces the base's at the same path
```

Prefer patches to whole files for definitions the game already has: two mods can then each change
one field of the same definition, and a mod copies nothing it does not change. A file that is not a
definition (a scene, a stylesheet) can only be replaced whole.

`mod.yaml` says what the mod is (`Host::PackageDescription`, `source/engine/host/include/jettison/host/Package.h`):

```yaml
version: 1            # the format of this file, not of the mod
name: My Mod          # what the player's list of mods shows
release: 1.0.0        # the mod's own version, which a save records it by
engine: 0.1.0         # the engine version it was written for; a newer one than the player's is refused
requires: [othermod]  # the mods it needs, each enabled and before it in the player's list
```

The identifier is the directory's name, so keep the name when you pack it. Raise `release` with
every release you give out, since saves name the mod by it.

### Build the script, if there is one

A script is WebAssembly, written in Rust or C against bindings generated from the game's types
([scripts](architecture.md#scripts)). Write the bindings for the build you target with the console's
`script_bindings <directory>`, which writes `jettison_script.rs` and `jettison_script.h`; a script
built against another build's bindings fails to compile rather than misreading. Build the module
with `jettison_add_script` (`source/CMakeLists.txt`) into the mod's `scripts/`, and declare it in
`defs/scripts/<name>.yaml` with the capabilities it needs and the events and inputs it answers.
The script suite builds its own modules so (`source/engine/script/CMakeLists.txt`, its
`tests/scripts`), in C and in Rust.

### Pack it

`jettison_pack` writes the pack:

```bash
cmake --build --preset clang-release --target jettison_pack
build/clang-release/source/tools/pack/jettison_pack path/to/mymod mymod.pack
```

`--level <1-22>` sets zstd's level for the files worth compressing, and `--directory <virtual path>`,
repeated, packs only the directories named rather than everything under the mod. The tool logs the
files it packed and how many it compressed, and warns of any file no virtual path can name, which it
leaves out. The same files give the same bytes. A mod built with CMake can pack itself as part of the
build with `jettison_add_pack(<target> SOURCE <dir> OUTPUT <file>.pack DEPENDS <staging target>)`.

### Try the pack

Run the pack the way a player would, from a user directory of its own so your own mods and saves
are not in the way (mods are read from `<user directory>/data/mods`):

```bash
mkdir -p "$TMPDIR/try/data/mods" && cp mymod.pack "$TMPDIR/try/data/mods/"
build/clang-release/source/test/render_test --user-dir "$TMPDIR/try"
```

The log should say that `mod:mymod` was mounted as a pack, the registry should name each definition
the mod changes, and a script should say it loaded; the console's `scripts` says what each loaded
script has done. If the directory and the pack are both installed, the second is reported and the
directory kept, so take the directory out to try the pack.

### Release it

Give players `mymod.pack` alone. They put it in their mods directory, which for the engine's shell is
`~/.local/share/jettison/mods`; a game on the engine may keep its user data elsewhere. A mod found
there and not yet in the player's list (`mods.yaml`) is added to its end, enabled, and the player
orders and enables mods from there.

### What is not there yet

- No prebuilt SDK: `JETTISON_INSTALL` is not a supported path yet
  ([consuming the engine](build-and-gates.md#consuming-the-engine)), so building the pack tool and a
  script needs the engine built from source.
- A pack is checked file by file, each file with its checksum, but not signed as a whole.
- A pack is read as it was mounted: replacing it on the disk takes a restart to see.
