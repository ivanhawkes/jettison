# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–13, the catalogue entries for the systems they
built, and the work finished since are recorded in [history](history.md).

## Current priority: neutral SDK and Manifest integration

Approved 2026-11-15. What is done, and what each Manifest task named of the engine, is in
[history](history.md#neutral-sdk-and-manifest-integration). Manifest's Stages 1 to 3 are closed
on the pin `30ea3f3`, Stage 3 (A02 to A07) with no engine gap. That stage's cross-review named
one, a service loaded with the commands a save queues, provisioned in
[history](history.md#neutral-sdk-and-manifest-integration). Its N01 (2026-11-20) settled the network
seam ([decided](decisions.md)), and N02 is done in both (2026-11-20,
[history](history.md#neutral-sdk-and-manifest-integration)): the engine's `net`, and Manifest's
dedicated server and TUI client on it, on the pin `65846bb`. N03 is done in Manifest with no engine
edit (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)). Its W02 is done in
Manifest (2026-11-20), a planning task: its hosted step runs Jettison's movement systems at 1/60 s and
names one engine gap, an order between `dock` and `lanes`, provisioned ahead of W05 (2026-11-20,
[history](history.md#neutral-sdk-and-manifest-integration)). W03 is done in Manifest with no engine
edit (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)). W05 is done in
Manifest (2026-11-20) on the pin `377bd8d`, its ships travelling through the docks, the cruise and
the flight out of a station's reach, with two provisions here: `Space::ClearOf`, and a steered ship's
controls let go with its destination ([history](history.md#neutral-sdk-and-manifest-integration)).
W17 is done in Manifest (2026-11-20) on the pin `54fb871`, its mode cruising by acceleration,
manned or unmanned by a hull's occupants, Astralis on the power law, with one provision here, the
cruise by acceleration ([history](history.md#neutral-sdk-and-manifest-integration)). W16, new
onboarding, is done in Manifest with no engine edit (2026-11-20,
[history](history.md#neutral-sdk-and-manifest-integration)), and so is W15, the vertical slice
(2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)), and so is W04, local
production, finite brokers and demand (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)),
and so is W06, station life support and resupply (2026-11-20,
[history](history.md#neutral-sdk-and-manifest-integration)), and so is W07, live and stale market
observations (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)), and so is W08,
company-funded cargo upgrades (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)). Its L26, the console's long
step, is done in Manifest (2026-11-20), the step taken a slice at a time and stoppable, with one
provision here, the rails placed once a body, which halved a cruising step
([history](history.md#neutral-sdk-and-manifest-integration)). Its W09, the complete trading fixture, is
done in Manifest with no engine edit (2026-11-20, [history](history.md#neutral-sdk-and-manifest-integration)).
Its L27, the fast-forward over quiet steps it asked for after L26, is done (2026-11-20), provisioned
here ([history](history.md#neutral-sdk-and-manifest-integration)): Manifest's systems say when they
next run and its console and server pass over the steps between, Ore to Brine 2.4 s from 4 min 50 s.
Its W10, standing trade policies and NPC jobs, is done in Manifest (2026-11-21) with one provision
here, a stopgap for the way in to a pad, and its L28, a docked fleet's wait passed over, with no
engine edit ([history](history.md#neutral-sdk-and-manifest-integration)). Its W18, W11 and W19 are
done in Manifest with no engine edit. Its W21, fuel burn, asked for a cruise's coast phase, a share
of the time thrusting, provisioned (2026-11-22, [history](history.md#neutral-sdk-and-manifest-integration));
and its slice 4 a station's reach measured where physics placed it (2026-11-22,
[history](history.md#neutral-sdk-and-manifest-integration)); W21 is done in Manifest (2026-11-22).
The station traffic lanes booked after it were measured and are parked ([carried
forward](#then-robustness-tools-and-what-a-mod-can-reach), 2026-11-22). The space extraction they waited
for is done (2026-11-23, [history](history.md#neutral-sdk-and-manifest-integration),
[its plan](space-extraction-plan.md)): the space setting is Manifest's, the space sample retired, and
Manifest pinned to `14fc23a` with every pin held. Formats change in place until player testing
([decided](decisions.md)). Manifest's W12, W13 and W14 are done with no engine edit (2026-11-23,
[history](history.md#neutral-sdk-and-manifest-integration)): its Stage 4 is closed and cross-reviewed.
Its R01, the save and compatibility contract, is done (2026-11-24), a planning task with no engine
edit ([history](history.md#neutral-sdk-and-manifest-integration)); its R02, durable I/O and the world
lock, is done in Manifest (2026-11-24) on the pin `ea66afe`, with two provisions here, directories
made durably and a file lock ([history](history.md#neutral-sdk-and-manifest-integration)), and with
them the message several datagrams long its Stage 4 review booked, which needed a limit set and no
splitting of the engine's own. Its R03 and R04, the journal flushed every 30 s and its hourly snapshots, are done in Manifest with
no engine edit (2026-11-24, [history](history.md#neutral-sdk-and-manifest-integration)), and so is
its R05, a clean stop, backups and restore (2026-11-24). Its R06, the runtime distribution contract, is done (2026-11-24), a planning task
([history](history.md#neutral-sdk-and-manifest-integration)): a standalone bundle for glibc 2.39 or
newer, with its TUI, and one engine gap, ImGui's backends out of `Devtools`, provisioned here before its R07
(2026-11-24, [history](history.md#neutral-sdk-and-manifest-integration)). Its R07, the distribution
built, staged and run in Ubuntu 24.04 and Debian 13, is done in Manifest with no further engine edit
(2026-11-24, [history](history.md#neutral-sdk-and-manifest-integration)), and so is its R08,
persistent reconnect and recovery (2026-11-24, [history](history.md#neutral-sdk-and-manifest-integration));
its R09, the close of its Stage 5, is next, with one provision here ahead of it, a world listing the
names its save holds (2026-11-25, [history](history.md#neutral-sdk-and-manifest-integration)). Its later workstreams wait until the user has played the first loop for a day or two.
Follow [game integration](game-integration.md) and inspect Manifest's current development-stage/gameplay requirements before each task.
Provision their reusable engine capabilities here; production rules and content stay
in Manifest. Retain the small space example as a separate automated-test consumer.

Each row carries the Manifest task it is ([delivery order](../../manifest/docs/delivery-order.md))
and the Manifest stage it unblocks; work them in Manifest's order.

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Encryption | before Manifest hosts outside a trusted network | A key exchange in the hello with the server's public key pinned by the client, then every packet sealed, on the libsodium `net` already links ([D-081](../../manifest/docs/decision-ledger.md#d-081)); a client with the wrong key is refused, and the suite and consumer run sealed. |
| Manifest capability integration | each of the above | Each required mechanic maps to a public API or a provisioned reusable gap, with independent engine and Manifest acceptance evidence; immutable SDK pin updated after engine validation. |

These deliveries precede discretionary expansion of the sample. The historical space
features below and in [decided](decisions.md) describe existing coverage and possible extension work,
not permission to make space gameplay a neutral engine requirement. Completed phases
remain historical evidence, not evidence that this new boundary is already implemented.

## Carried forward

Work that phases 1–13 moved on, in the order it is worth doing: by what is expensive to change later,
then by what the game's next work stands on, as the phases were ordered. Within a tier the first is
the most pressing. The decisions they waited on are settled, under [decided](decisions.md). Phases
1–13's full record, and catalogue entries 1–31, are in [history](history.md).

### First: what saves and the simulation's interface hold

Each of these fixes what a save holds or the shape of what goes into and out of the step, and gets
dearer with every save a player has and once a second player sends commands.

- **Who a script commands as, under networked play** (with N01, N02): the host now replaces every
  top-level `Sim::Controller` of a script's command with the one sender `SendCommandsTo` was given,
  and the shell gives none ([architecture](architecture.md#scripts)). With several players the
  server is authoritative over whose context a script runs in, call by call: a handler run for a
  player's event sends as that player, an NPC's as that NPC, and no client or script can name
  another player or an NPC. It also reaches a `Sim::Controller` nested inside a command's fields,
  which the host does not yet stamp.

### Next: what the game's next slices stand on

Consult Manifest's current stages for the next required mechanics. The engine provisions
reusable capabilities; the following space-example backlog is subordinate to the neutral
SDK work above and does not transfer production game rules into Jettison.

- **Manifest integration, not sample expansion** ([game integration](game-integration.md)):
  provision public world/definition/command/persistence/script capabilities required by the
  game's current stages. Its order book, economy and progression remain in Manifest.
- **Decisions, not yet placed** (entry 26, 9f): reactive nodes, which tick a condition above a
  running branch and halt what they leave, as a trader breaking off its run to flee would need, and
  with them a task's halt; parallel nodes; a tree that names another as a subtree, where the
  trader's tree repeats its three-node jump loop for each leg; a blackboard of the engine's, where a
  trader's run is its own component; a command that gives an entity a tree, with its sender, where
  the shell gives a trader its tree as it makes it; and a tree ticked less often than every step,
  which seven traders do not need. A trader's run that cannot be made waits half a minute and
  chooses again; what a trader does about a hull it cannot dock, a market that has gone, or the
  player in its way beyond steering clear is the game's.
- **Scripts, not yet placed** (entry 29, 11b): a script reading the world, which today it sees only
  through the events it is given and an option's inputs (decided, 2026-11-13: a handler reads the
  newest stepped snapshot, by component, as granted, and acts as its player; an input reads only its
  inputs); a script called from a behaviour tree's task, as an input is from a consideration; a script reloaded
  when its module changes; scripts compiled ahead of time; and the fuel a call used, which WAMR does
  not report, for the console. A script's own reasons (`reason.script.<script>.<name>`, decided),
  which wait for something a script refuses; and the sender of a command a script sends, which it
  sets itself, as a `Trade`'s (12c), until a handler acts as its player.
- **Packs and mods, not yet placed** (entry 10, 11a): the game's menu over `mods.yaml` (12a), which
  the player edits by hand until then; a patch that removes an element from a list, rather than
  replacing the list; a pack hot-reloaded, which is read as it was mounted; and layering for anything
  but definitions, so a mod still replaces a scene, a system or a stylesheet whole.
- **Picking a paused world, and under the cursor every frame** (entry 22, 7c): a pick is answered by
  the next step, so a paused world answers when it resumes, and a hover highlight would send a
  command a frame. Both want a query presentation makes itself, against the bodies' shapes in a
  snapshot. Not yet placed.
- **Physics, not yet placed** (entry 22): a shape offset in its body, and compound shapes; collision
  filtering beyond static against moving (the game's own layers and groups); Jolt's debug renderer
  drawn through debug draw; Jolt's job system on more than one thread, once a profile asks for it
  (deterministic either way); the capacities tuned against real traffic; overlap queries (every body
  in a shape) and casts that return every hit, which steering (9b) did without (below).

### Then: robustness, tools and what a mod can reach

Cheap to add when they are wanted and costly to nothing meanwhile, but for the log, whose file a
player's report of a problem needs and the shell does not write.

- **The way in to a pad, done properly** (the user's, 2026-11-21; entry 31). First in the tier:
  every NPC and autopilot trade in Manifest docks through it. Manifest's W10 zero-client run found a
  ship that came round its station fast and sideways overshooting the point above its pad, which is
  only `kAboveMargin` (10 m) outside what steering keeps it from: each lap avoidance commanded it
  out at up to 190 m/s and Arrive pulled it back, and it circled 30 to 55 m off the point at about
  40 m/s until its 300 s reservation lapsed. In a grid of 48 starts round Highport, 2 km and 4 km
  out, still or going across at 150 m/s, two never reached the point. Ship and station shared one
  frame throughout. A 100 m margin alone cured that case; a waypoint out along the normal, which
  steering passes at full speed, put a sample trader into Ostra Anchorage at 126 m/s. The stopgap
  (`Docks::FlyInbound`, `FlyAbove`): the ship stops at a point out along the normal as far beyond
  the point above as it stops in from its top speed, then flies straight down the normal from rest,
  so the 10 m margin stands; from 10 km Highport's shuttle is over its pad in 117 s, 111 s before.
  What other games do, from recollection and not checked against sources: EVE has no approach,
  docking at once within range; Freelancer and many older games fly to a ring or entry point and
  script the rest; X4 and Star Citizen's traffic control fly authored approach paths, holding point,
  approach point, pad, with avoidance relaxed against the station; Elite Dangerous gives a pad and a
  permission that lapses, limits speed near a station and flies its docking computer down a fixed
  approach; Space Engineers' general autopilot is known for circling and overshooting its
  waypoints, the failure here, which players work round with a slow precision mode or scripts. So
  the solution: an approach corridor flown without stopping, at a speed that falls with the
  distance left; the station not an obstacle on the corridor's last leg, as it is not on the way
  down (`byShape`); Arrive damping the velocity across its line rather than pursuing; and a
  reservation that lapses when a ship stops making progress rather than at a fixed time. Measured by
  ships flown in from every side and speed, one queued behind another on the next pad, and no
  arrival slower than the stopgap's.
- **Station traffic lanes, parked** (the user's, 2026-11-22). They waited for the engine's space
  concepts to leave its neutral modules, done 2026-11-23: the lanes are Manifest's `space`, on the
  engine's slots and portals. Booked after Manifest's W21, for
  departures, arrivals and cruises sharing one line between the five stations of Ore's one orbit, which
  its policies hop round. Measured first on the engine as it is, by Manifest's `fixture_traffic`: six
  starters flown both ways between Ore Highport and Vale, two sent in one step as a policy sends them
  and two leaving Highport as two come in, came no nearer than 198 m hull to hull, between steps as
  well as at them, against steering's 20 m clearance; leaving took 52 to 70 s and coming in 87 to 98 s.
  Opposite flows do not share a line on an orbit: each cruise aims where its target will be, and the
  stations move some 15° an hour, so the two ways' courses in one hour are 5,400 km apart. What is
  shared: ships going the same way fly one line 6 to 70 m apart, kept apart by leaving seconds after
  one another, and a departure passes some 800 m from an arrival; a station's pads bound how many
  leave at once. Taken up when `fixture_traffic` fails or traffic it does not cover asks (a
  station of more pads, larger hulls, a busier orbit), on the design settled with the user: every
  cruise from a station flying one of several lanes each way, kept to the right of its way as roads
  and Freelancer's paired trade lanes do, held as pads are and kept from gate to gate, departures
  leaving above the orbit's plane and arrivals coming in below it; the lanes first, the way in's
  corridor after.
- **A test of an acquire out of date twice** (the user's, 2026-11-18). `drawEveryFrame`'s recreate
  loop and its failure after eight are not covered: it needs fault injection under the Vulkan
  layer, which the engine does not have. Taken up when that branch is needed, if ever.
- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, so its reports
  reach only the default stderr sink. The run's reports (pass timings, the cluster check, the
  benchmark) went through `Log.h` on 2026-11-17; the failure report, usage line and teardown stay on
  `std::cerr` ([coding standards](coding-standards.md#errors)).
- **Crash handling, not yet placed** (entry 30, 11d): a report from a thread other than the main
  one whose stack overflowed, which wants an alternate stack set by every thread the engine starts
  (the I/O worker, the scripts' watchdog) and by Jolt's; each module's build ID in the report, and a
  release's debug files kept by it, and the shell saying on its next start where a new report is
  (decided, 2026-11-13); a report sent anywhere, which waits on players outside development; a
  minidump, should a report's frames and tail not be enough.
- **A destination prefetched** before it is shown (entry 21): the renderer asks for a model when it
  first draws it, so a jump holds its effect over the new system's first frames while they stream
  in, 0.2 to 0.4 s on lavapipe for the sample systems. Not yet placed.
- **A glTF model's collision mesh** (entry 22, 7b): a station built to a layout collides as its
  decks and piers (9c), as every station of the sample systems is, and one of no layout as the
  procedural shapes the scene draws it with (`app/src/Piloting.h`); a hull collides as the box of its
  definition's size, whether or not it names a model to be drawn with (9d). A model's triangles, read from its file on
  the I/O worker as its rig is, or a collision mesh beside it, are not yet placed, and a model on a
  station of no layout is reported and left out.
- **Steering, not yet placed** (entry 26, 9b): an obstacle is the sphere about its body that holds
  its shape, and only a destination kept clear by its hull (9c) flies inside one, by a shape cast
  along its way, which slows it and does not turn it: a course round the inside of a station, from
  one pad to another under a pier, is not plotted, and a docked ship's way to its pad is straight
  down from outside the sphere, which a layout is checked to leave clear. Steering is measured for a
  station's traffic, sixteen ships; a busier lane's, where every ship queries the index each step,
  is measured when a game has one.
- **The UI, not yet placed** (entry 23, 8b): a stylesheet or document reloaded when its file
  changes (RmlUi keeps a stylesheet for the run); a document's images decoded on the I/O worker
  rather than as the document lays out, which is on the main thread and fine for a screen's icons;
  RmlUi's debugger in the dev tools.
- **Localisation, not yet placed** (entry 30, 11d): numbers with fractions, dates and times in the
  language's own form, where only a whole number's thousands are; text a document holds in an
  attribute, which RmlUi does not hand to the table; right-to-left and shaped scripts, which RmlUi
  can lay out with HarfBuzz and the fonts shipped cannot draw; and the player's language chosen at
  the first start from the platform's (Steam's, the environment's), which the setting stands in for.
- **Comments better as names** (the comment review, 2026-11-18). `kFullTurn`
  (`animation/src/Playback.cpp`) as `glm::two_pi<float>()` and `kBelowOne`
  (`time/src/FixedStep.cpp`) as `kLargestBelowOne` (`std::nextafter` is constexpr only from C++23),
  each then needing no comment; and the trailing comments, which `comment-only.sh` cannot tell from
  code and so were left: `Theme.cpp`'s glosses on the console lines' tokens and the trailing comment
  `FontFaces.cpp` wraps over three lines.

### Last: presentation, when a scene asks for it

Each is placed by the first scene or screen that needs it, and changes no data or save on the way.

- **Animation, not yet placed** (entry 27, 10d): a glTF clip's events, which glTF has no place for
  (its extras, or a clip file beside the model); one clip blended into another, so a clip changed
  part-way, as a pad's clamps let go before they have closed, starts the new one from its first pose
  rather than from where the rig is; the clamps fitted to the hull's box rather than its shape, so
  on a hull drawn by its model they grip where its box's sides are; and rigs of the ships' own
  (landing gear, a cargo hatch), which the hull proxies do not have.
- **The audio, not yet placed** (entry 24, 8c): music that crossfades between pieces and a playlist
  to choose them; Doppler, once something reports velocities to presentation; the ship's own sounds
  (its engines, the cockpit's) and occlusion by hulls; a sound file streamed from its mount rather
  than read whole, which waits on a streaming read in the VFS (entry 10; a pack's stored files could
  be read in ranges, its compressed ones would want zstd's streaming decoder). The voice bus has
  nothing on it yet.
- **Cinematics and grading, not yet placed** (entry 30, 11c): a jump's cutscene, which the
  sequencer plays as data but the jump does not yet wait on (its fade rises as it is asked for), and
  whose arrival would bind the gate, which the shell does not; a sequence's events heard by the shell,
  as sounds by data; a shot's roll and depth of field; a blend that steers round a hull rather than
  along a line, which a rail is drawn to avoid today (the dock's starts on the chase camera's side);
  and a grade blended between two tables, as a sequence's own look or a system's change of one
  would want.
- **The UI's drawing, not yet placed** (entry 23, 8b): RmlUi's clip masks (an element with
  `overflow` clipped by a rounded border, or clipped while transformed), which want a stencil in the
  output pass's rendering, where the UI is drawn into the swapchain and the depth target's stencil
  (10c) is not attached; and its layers, filters and shaders (`filter`, `backdrop-filter`,
  `mask-image`, gradients drawn as shaders), which want render targets of their own, as the post
  chain's are (10c). Until then RmlUi's defaults draw nothing for them, and a screen uses none.
- **The transparent pass, not yet placed** (entry 28, 10b): particles sorted within an emitter, on
  the GPU, which a smoky emitter seen through itself would want; soft particles where they meet a
  surface, which want the depth the opaque pass is still writing, so a node of their own after it;
  refraction through glass, from a copy of the frame behind it, which the opaque pass's resolved
  target would give were the transparent draws a node of their own after it; a glTF primitive's `BLEND` alpha mode, which draws opaque; a transparent
  surface's shadow, tinted, which the sun's depth-only cascades cannot hold; and an emitter
  started warm, as if it had been running, which a capture of a smoke column waits three seconds
  for instead. A ship's thrusters are shown as its proxy's class places them, a hull drawn from a
  model has none, and a hull's definition cannot place its own ports yet.
- **The post chain, not yet placed** (entry 28, 10c): a transparent part of an outlined shape (a
  canopy) is not in the depth prepass, so the shape's outline runs round the opaque parts alone; a
  pick read from the object IDs under the mouse, which the physics ray answers today; the game UI and
  the dev tools under a mask, since they are drawn in the output pass, where the stencil is not
  attached; ambient occlusion at half the frame's size, or kept from frame to frame, should a
  measured frame want it cheaper.
- **The dev tools' theme, and what it leaves** (ui/imgui): the style is themed 2026-11-17, on
  Catppuccin's four varieties with the spacing of a Tailwind sheet, switched live by `dev.palette`
  and `dev.ui_scale` ([architecture](architecture.md#ui)). Not yet placed: the game
  UI's own stylesheet in the same palette, so the market screen and the tools read as one design,
  which is a RmlUi stylesheet over the VFS and no engine work; the palette as a player setting on the
  game's own screen, which wants the settings screen the sample does not have; a light-attachment
  swap noticed mid-run, which `ApplyThemeIfChanged` answers only when a setting changes; and the
  darkest tones exact on the palette, which wants the sRGB encoding done in the UI's own shader over
  a UNORM attachment — ImGui packs a vertex colour into 8-bit linear bytes, so `#1e1e2e` lands as
  `#1c1c2e` and `#11111b` as `#0d0d1c`; measured over the 264 channels of the four varieties, 209
  land exact, 33 are one step off and the most is four, all four-step cases at the dark end
  ([architecture](architecture.md#ui)).
- **Not yet placed** (entries 19 and 20): the sun's shadow distance and split blend tuned by
  measurement; hysteresis between levels of detail, and levels for model files; the planets' own
  shadows past the shadow distance (a station in eclipse), which would need an analytic test against
  the bodies; programs that hot-reload; cube maps and texture arrays in files; normal mapping in `mesh`, which
  waits for tangent-space vertex data (`math/Tangents.h` makes it; `planet` builds its frame from
  the sphere and does not need it); a glTF model's own
  metallic-roughness factors and textures; and a model file read once rather than twice, for its
  rig and for its geometry (6a).

### The samples' backlog

The space and ground samples' own loose ends and looks, for when a sample is extended. None is an
engine requirement, and none comes before the neutral SDK work above.

- **The ground sample's frames held** (the user's, 2026-11-18, after G01): `scripts/capture.sh`
  runs `render_test` alone, so the ground sample's scene and chase camera were checked once, by eye,
  and no reference view holds them as the space sample's are held. The script taking the executable
  to run, and a ground view in `source/test/frames/views.txt`, would. Before L15's graphical client,
  when a second game's look starts to matter.

- **A screen to buy ships from** (the user's, 2026-11-14, as 13d began): 13d buys a ship with the
  console's `buy <hull>` and lists and attaches to the fleet with `fleet` and `attach`, ] and [; a
  shipyard screen of the game UI's, beside the market screen, with the hulls a station sells, their
  prices and the purse, is held over for a later phase, as is a fleet screen.

- **Cruising, not yet placed** (entries 22 and 26, 13a): a cruise to a ship rather than a body;
  one broken off part-way, as a privateer's would be, which knowing where the ship is every step
  allows; a course that bends round a body rather than turning at waypoints, which the ship does in
  a step; and a body in the way placed where the ship passes it by its time alone, so a planet
  neither end orbits is placed half-way and widened by half of its own motion. A cruise's arrivals
  are spread clear of each other as a lane's are (13b), but a ship that comes out at rest is not
  made to move off if another then flies into its place.

- **Gates, not yet placed** (entry 26, 13c): a ship waiting its turn at a gate is not placed, so a
  trader waits where its cruise left it, spread from the others only as arrivals are; a clearance is
  given up only by lapsing or by its ship leaving the gate's frame, and no command gives one up; a
  licence or a standing, which the clearance's check takes when the game has factions (decided,
  perhaps as an input a script answers). The console's `jump`, the J key and the galaxy panel's
  buttons are the tools' jump, which sends the ship along a lane wherever it is, past the gate and
  its toll, as the smoke sequence and the reference frames use it; it goes, since all travel is by
  gates (decided, 2026-11-14), once they fly through a gate instead; the smoke sequence does not wait
  the 40 s for the ship to come out, and the AI and app suites cover the arrival.

- **The ground sample's loose ends** (the user's, 2026-11-18, after G01): its ground has no name, so
  a pick logs it as `Entity 0x0`; and its rover is a box sliding on the ground's friction, which
  needed 8 rad/s² to turn and drifts sideways, not a vehicle that grips. Sample polish, whenever the
  sample is next touched; nothing stands on it.

- **A body's look as data** (entry 21): the system scene draws a body it builds as its kind is, from
  a table in code. A station's look is its layout's, dressed (9d); a planet's material, air and
  rings are a scene file's (10a), so Corva's rings are drawn in `scene/vesper.yaml` and not in the
  Vesper a jump builds from its system file. A look per planet, moon or kind, as a scene fragment a
  mod can shadow, is not yet placed.

- **The station dressing, not yet placed** (9d): the dressing is drawn and not collided with, so a
  ship flown off its pad's column passes through a floodlight pole or a display ship, which the
  column a layout is checked to keep clear never meets; a pad's look as data (its paint, its poles'
  spacing and its floodlights' aim are the shell's, by class, in code; a reserved pad's lamps blink,
  10c, by a material of the shell's choosing); the floodlights' shadows,
  which the clusters' lights do not cast. The light-budget scene (`scripts/make-light-budget-scene.py`)
  still adds its 265 lights where 5e's hand-built hub was, now inside the layout's hub, so it measures
  the grid's cost and no longer shows anything; the dressing's 104 are the station's lights now.

- **A nozzle's glow, to revisit** (13e): a thruster's glow is its procedural nozzle's mesh colour,
  which is in the shape's vertices, and the renderer keeps one model a colour (`Render::ModelCache`),
  dropped the first frame none draws it. Shown as it was, every frame a new colour, it made and
  uploaded a model for every nozzle every frame, jerky in a release build; it is now shown in eight
  steps that every nozzle shares (`kGlowSteps`), which bounds the models at nine but steps the
  ramp. The method is to be revisited: an emissive scale a draw carries per entity (a mesh's own,
  or a material parameter set per draw), so a glow is smooth and costs no model, and a nozzle of a
  hull drawn from a model can glow too.

- **Air over a mesh in it** (entry 28, 10a): the haze is the planet's own surface's and the sky's,
  not a ship's or a station's within the air, which no sample scene has yet, nor a transparent
  draw's (10b); nor are the bodies' air and rings sorted where two overlap on the screen. Not yet
  placed: the first scene with a ship in the air brings it, for the opaque and the transparent draws
  alike.

- **The planets' look in the environment's capture** (entry 28, 10a): the capture draws a body as
  a sphere of its mesh's colour, without its maps or clouds, which would about double Ostra's
  brightness in it. Not yet placed.

- **A gas giant's bands** (entry 28, 10a): Corva is one colour under its air. Not yet placed.

## Stubs

Phase 1 wrote a stub for every module in the catalogue below; the modules under
`source/engine/` are the list. A stub is:

- the module's directory, `CMakeLists.txt` and its place in the dependency graph
- its public header(s), with the main types and functions declared and documented: what they
  are for, what owns what, and which phase fills them in
- an implementation that compiles, does nothing observable, and fails loudly where doing
  nothing would be wrong (`JETTISON_NOT_IMPLEMENTED()` from `core`)
- one doctest test that the module links and its stub behaves as documented

A stub does not invent behaviour the phase that fills it would have to undo. Where a stub holds
configuration it was constructed with (a fixed step, a seed, a mount list), that much is real and
tested; everything that would need an algorithm throws.

## System catalogue

In priority order. Each entry is the brief for its stub. *Library* is the chosen dependency.
*Stub* names what phase 1 declares.

Entries 1–31, for phases 1–13, are in [history](history.md#system-catalogue-entries-131).

## Open decisions

Each is settled before the work it blocks; settled ones are under [decided](decisions.md).

None open.
