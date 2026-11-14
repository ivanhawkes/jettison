# Sample game

The engine has no game in it; the game lives in its own repository ([scope](scope.md)). The app
shell plays the game's part wherever a system needs one to be seen working, as it did for piloting
and docking (phase 7), and from phase 8b that part is a small sample game: the first slices of the
game's own plan, built just far enough to exercise each engine system as it arrives. This document
records where that plan comes from, what of it the shell plays today, and what comes next.

## The plan: Manifest

The initial planning stage for gameplay is the **Manifest** project (the Civilisation Simulation
until 2026-11-12, its simulation core CivSim then and ManifestCore now), which lives beside this
repository at `~/projects/manifest`, with its GitHub repository at
[ivanhawkes/manifest](https://github.com/ivanhawkes/manifest). The local copy is the authority: the
GitHub repository lags behind it and may hold work on branches other than `main`. It does not build
at present, and is read as a plan, not run.
Three parts of it are the plan:

- **`idea.md`**, the raw ideas: a space trading empire after the silk road and the early
  East India trade; currency in credits at a credit to the US dollar, priced whole, with small goods
  sold by the package; stations in orbit as trade hubs, several to a planet, one above each of its
  economic blocks, each a market of its own and so an arbitrage on a planetary scale; black and grey
  markets on unlawful stations; surveys bought by licence and worked with rented machinery; ships
  with repair state and a cockpit view, one flown directly and the fleet managed through a map;
  insurance after the early Dutch corporations; privateers; star gates around suns for the long
  hops and moon gates for the short; economies driven by GDP and purchasing power, corruption,
  health, education, energy and pollution; planetary turns every 200 ticks and market ticks every
  20; a player who starts with a small ship, some credits and a full hold of cheap goods on a poor
  world; time compression and pause.
- **`README.md`**, ManifestCore as built: generated worlds with an age, a population, production,
  demand and a stockpile, priced by scarcity; a skill tree of tiers, prerequisites and exclusive
  groups, whose effects land on hulls, venues, worlds, lanes, surveys and yards; and a marketplace of
  venues at bodies, each a price-time order book, trading whole units for whole credits, with a
  listing fee (40 basis points by default) and an insurance premium (120) on each side, and orders
  that expire after 24 ticks.
- **`data/`**, the content: the ages from Stone to Information, the categories of goods, some 170
  trade items with their physical units, the skill tree, and the solar systems' name pools. An item
  carries its density, its unit, its units to the cubic metre, its pack factor and a 2025 price, all
  against the IBC cube (a one cubic metre intermediate bulk container), so a cube of any good has a
  mass and a value.

Manifest's own `idea.md` asks for it to be refactored onto Jettison before more work is done on it, so
the plan and the sample game converge: what the shell builds here is written against the engine's
modules, in the engine's style, and the game takes it over from there.

## What the shell plays

| Plan | In the shell | Since |
|---|---|---|
| Goods with physical units, priced in credits | Eight goods from Manifest's data, as definitions (`assets/defs/goods`): wheat seeds, flour, beer, iron, copper, phosphorus, antibiotics and electronics, from 290 to 144,000 credits. | 8b |
| A unit of trade that hides fractions of a credit | The crate: one IBC cube of the good, its price the plan's price of a cube (copper is 7,616 kg of ingots at 10 credits a kilogram, 76,160 a crate). | 8b |
| A market at each station, each its own economy | A market at each of the galaxy's three stations (`assets/defs/markets`): Ostra Highport over a farming world, Ostra Anchorage in low orbit over a mining block of the same planet, Corva Refinery at an industrial one. Each holds a stock of every good it deals in and makes and uses it at its own rates each market tick, ten seconds, up to four times its target stock. | 8b |
| Prices that follow scarcity | `base x 2 x target / (target + stock)`: twice the base with none on the shelf, the base at the target, a quarter of it at seven times the target and beyond. Whole credits, integer arithmetic, deterministic. | 8b |
| A venue's charges | A listing fee in basis points on each side of the price (150 at Ostra, 120 at Corva): the ask rounded up and the bid down, so the market never pays out a fraction of a credit. | 8b |
| A small ship, some credits | The shell's ship: a hold of twenty crates; the player's 250,000 credits, their own rather than the ship's (12c), as each trader's are. | 8b |
| Trade when docked | A `Trade` command, buy or sell, applied in the step for the ship's owner, whose credits pay: refused, with a reason the screen words in the player's language (12c), for a sender that does not own the ship, a ship not docked at the market's station, too few crates on the shelf or in the hold, too few credits or too little room. | 8b |
| A trader's screen | The market screen (M): the station's goods, stock, bid and ask, each good's galactic mean, what the ship holds, and the best bid among the other markets, with Buy and Sell by one crate or ten. The galactic mean starts at the good's base price and follows the galaxy's trades, the player's and the traders', weighted by their crates; a bid over it, worth selling at, and an ask under it, worth buying at, are green. Bound to the simulation's snapshots. | 8b |
| Saved games | Markets, holds and the owners' purses are simulation state, saved with the world; the saved-games screen (L) loads one. | 8b |
| A market's answer you hear | A trade filled and a trade refused sound on the UI bus, two notes rising and two falling (`assets/defs/sounds`), mapped from the events by data. | 8c |
| A station you come home to | Docking and undocking sound on the effects bus from the pad, as its clamps close on the ship and let it go (10d); the station hums as the ship nears it; a loop of music plays. All synthesised by `scripts/make-sample-sounds.py`, so nothing in them is anyone else's. | 8c |
| Lanes between the systems, star gates the long hops | The ship crosses a lane: J, with the ship in the system shown, sends it through the gate, 40 s to Vesper, out at rest 4.6 km from Vesper Gate, facing Corva, and bound to the system as it was to Ostra's. Each gate is in Manifest's place for it: a moon gate about a planet (Halcyon Gate about Ostra, Vesper Gate about Corva) and a star gate about a sun (Vesper Deep Gate, Marrow Gate), facing a planet, so a ship comes out in the gate's own frame, a few kilometres from it, and crosses to a station by cruising. The console's `route` gives the way to a system and its time. | 9a |
| Other ships about the stations | The console's `traffic` sends shuttles from a kilometre or two out to hold station on a ring about the station, up to sixteen: each flown by steering, giving way to the others, the station and the player's ship, and turned with the station once there. They choose nothing yet. | 9b |
| Stations as trade hubs, a pad for every ship | Ostra Highport at the plan's scale, 1.5 km across, with twelve pads for the largest transports on four piers, and Corva Refinery with four for transports and two for shuttles (`assets/defs/stations`), from the hulls of `assets/defs/hulls`. Three ships are parked at each, and the shell's ship starts docked on Highport's first free pad. The console's `undock` lifts it clear; `dock` asks for a pad and flies it in, round the station and down onto the pad, or leaves the landing to the pilot with `dock manual`. A ship waits its turn when every pad it fits is taken. | 9c |
| A full hold takes time to load | A trade's crates and price are settled at once, and the crates then cross the deck on buggies, four a trip, three to each of Highport's pads and two to each of Corva's, out of the bay at the root of the pad's pier and along the road down its side to the ship's hatch, at 8 m/s. The ship is held on its pad until every crate for it is aboard and every one off it has been taken from its hatch: a shuttle's twenty crates are aboard Highport's pad 1, 117 m from its bay, in 45 s, and pad 3, 633 m out, in four minutes. The market screen says how many are still crossing, `undock` is refused until none are, and the console's `trade <good> <crates>` trades as the screen does. | 9e |
| Stations that look like ports | Every pad painted (its outline, a centre line with chevrons to the bow, the keep-clear box over the hatch, its number) and floodlit from poles along its sides, a lamp on each pole green while the pad is free, amber while it is reserved and red while it holds a ship; the hub in the hull's plating, the piers in deck plating, and a door at each cargo bay. Highport's display wing, between two of its piers, shows a liner and a frigate on XL bays and four fighters on S pads beside them. Each class of hull has its own proxy: the shuttle, a transport of cargo pods behind a cab, the largest transport's racked containers under a bridge, and the display ships' long hulls. | 9d |
| The plan's other ships, trading | Seven traders, five transports and two shuttles, parked at Highport and Corva at the start (`app/src/Traders.h`). Each chooses a run by utility (`assets/defs/decisions/trade.yaml`): a good bought at one market and sold at another, weighed by its margin, by how many rivals are after the same shelf, and by its credits a second over the route, which is plotted across the lanes only for a run still in the reckoning; or, with nothing worth flying, it waits. Its behaviour tree (`assets/defs/behaviours/trader.yaml`) then flies the run: off its pad, round the station and out of its reach, across the system to the gate by cruising, up to the gate and through it, across the next system to the market the same way, onto the pad the station gives it or, with every pad taken, held off the station in its place in the queue; the trade, and the wait while the buggies carry its crates. Corva's one free pad keeps a queue. They read the same markets the player does, so the player buys against them, and the console's `traders` says what each is doing and has made. | 9f |
| A mod's rules as scripts | The smoke mod's script (`source/test/scripts/smoke.rs`, in Rust, as the shell's scripts are), from the user's mods: it hears each trade filled and says what it heard, and answers the traders' `crowding`, one over one and the rivals after the same shelf, which the mod's patch of `defs/decisions/trade.yaml` weighs a run by. The console's `scripts` says what each script loaded has done ([scripts](architecture.md#scripts)). | 11b |
| Gates that clear a ship, for a toll | Each gate clears one ship at a time to fly through its ring, which sends the ship along its lane and charges its owner the lane's toll as it enters: 500 credits a light year between the systems it joins (`assets/systems/galaxy.yaml`), so 2,000 for the moon gates' hop from Halcyon to Vesper and 6,000 for the star gates' on to Marrow; the rest wait their turn, and a clearance not flown lapses after two minutes. A ship not cleared flies through the ring and on, charged nothing. The console's `transit` asks the gate whose frame the ship is in (`cruise Vesper Gate` brings it there), the flight panel counts the clearance down, and the scene jumps with the ship as the gate sends it. The traders ask at each gate on their route, wait their turn and fly through, and weigh a run's tolls in what it makes. J and the console's `jump` stay the tools' jump, past the gate and its toll, until they go: all travel between systems is by gates. The shell opens on Halcyon, the ship docked at Ostra Highport. | 13c |
| A system crossed at its own scale | The ship cruises to a body of its system: the console's `cruise <body>` takes the shuttle, flown more than 2 km clear of its station, from Highport to Pell, 393,000 km, in 56 s, out at rest 477 km above the moon and facing it, round Ostra when the planet is in the way; out of the lane at Vesper Gate to Corva, the gate 500,000 km off it, in about a minute; to Halcyon's star in four minutes. A ship arrives no nearer a planet than where its gravity is a gentle 1 m/s², which its assist holds against. Each hull's drive is its definition's (`assets/defs/hulls`), and the flight panel says where the ship is cruising to, how long is left, and then what it is off. The traders cruise as the ship does (13b): to each gate on their route and from the last to their market. | 13a, 13b |

Two of the three stations are in Halcyon, a cruise apart, and the third is in Vesper, a lane away, so
the arbitrage the screen shows between Ostra's grain and Corva's metal is one the player can fly: buy
at Highport, cruise to Halcyon Gate, be cleared through and fly it, cruise from Vesper Gate to
Corva Refinery, dock and sell; and one the traders fly too, so a shelf the
player means to empty may be empty when the player gets there.

## What comes next

Each slice lands with the engine system it exercises:

- **A fleet.** Each player starts with one ship and buys another once their credits allow. They
  attach to any ship of their fleet to watch it go and to trade at the market it is docked at, and
  take the helm of one at a time; the rest fly by their own behaviours, as the traders do, for their
  owner, whose credits they share ([decided](roadmap.md#decided)), cruising across systems and
  cleared through the gates as the traders are.
- **The order book.** ManifestCore's price-time book, with its order life and its fills, replaces the
  market maker here once traders post orders of their own: the traders of 9f, then the player. It is game code ([scope](scope.md)):
  the engine's part is that it is deterministic, saved, and bound to a screen.
- **The economy's turns.** Planetary turns every 200 ticks over GDP, population, ages and the
  essential categories, which set each market's production and demand; the skill tree's effects on
  venues and hulls, through the definition registry.
- **Scripted rules.** The plan's rules a mod changes, through the Wasm host (11b): the traders' and,
  as they arrive, the economy's turns and the order book's, each an input or a handler a mod's
  script answers.

Not placed yet, from the plan: surveys, black and grey markets, insurance, privateers, a gate's
licence or standing, and the station's services (refuelling, repairs, intel).
