# Networking

The `net` module is the transport a game's dedicated server and its clients are built on. It
carries messages between one server and many clients over UDP, admits a client by a handshake,
bounds what each client may cost the server, and sends each client its own view of the world. It
knows nothing of what the messages mean: the protocol, the accounts, and what a client may do and
see stay in the game.

One stack, ENet over UDP, single-threaded and polled, serves an idle game's occasional requests and
a real-time game's stream of inputs and snapshots. Every message is sealed to keys the handshake
makes with the server's key, which the client pins, so a game may host on an open network.

## Concepts

| Term | What it is |
| --- | --- |
| Server | `Net::Server`: listens on one IPv4 address and port, admits clients and queues their messages. |
| Client | `Net::Client`: one connection to a server. It connects as it is made and is spent once closed; to reconnect, make another. |
| Net session | One connection on the server, named by a `SessionId`. Ids count up from 1 and are never reused, so a reconnection is a new session. It is not a [host session](glossary.md#session) (`Host::Session`, a running world). |
| Server key | The server's key pair (`Net::ServerKey`): the secret stays with the server, and every client pins the public half. |
| Protocol | The game's name for its protocol and a version number (`Net::Protocol`). A server lists the versions it accepts. A message's shape changing is a new version. |
| Credential | Bytes the client sends in its hello, opaque to the engine. Usually a bearer token. |
| Verifier | The game's function that judges a credential and admits or refuses the session. It records whom the session stands for, the game's *actor*. |
| Channel | `Reliable` (reliable and ordered) or `Unreliable` (unreliable and sequenced: a packet older than one already delivered is dropped). |
| Message | A `MessageKind` (the game's number for it, two bytes) and a body the game encodes, usually in the [binary archive](persistence.md#archives). |
| Snapshot view | The game's function that encodes what one session may see, which `SendSnapshots` sends. A network snapshot is this encoded view, not a presentation [snapshot](glossary.md#snapshot). |
| Reason | `Net::Reason`: why a connection ended, reported the same at both ends (`ReasonName` gives its text). |
| Token | A bearer token: 32 random bytes as 64 lower-case hex characters. The server keeps only its digest. |

### Reasons

| Reason | When |
| --- | --- |
| `Closed` | The other end closed, or nothing was heard from it within `Limits::timeout` (5 s). |
| `UnknownProtocol` | The hello names another protocol. |
| `VersionMismatch` | The hello's version is not one the server lists. |
| `ServerFull` | `maxSessions` are already admitted. |
| `BadCredential` | The verifier refused the credential. |
| `HandshakeTimeout` | No hello within `handshakeTimeout` (2 s) of connecting. |
| `BadMessage` | Anything but an offer and then a hello before admission, a hello that does not decode, a packet that does not open after admission, or a message of an engine kind. |
| `InboundOverflow` | A message arrived while `maxInbound` were already waiting for the game. |
| `Revoked` | The game closed the session because its credential was withdrawn. |
| `Shutdown` | The game closed the session, or destroyed the server, because the server is stopping. |
| `KeyMismatch` | The client's hello was not sealed to the server's key: it pinned another server's. The client also closes so when the server's proof, or its welcome, does not open: a server that does not hold the key it pinned. |

## How it works

### The network never steps the world

The server does nothing between calls, and every call is made from the thread that made it. So the
game decides when the network runs: it polls between its [steps](glossary.md#step), and the
[simulation](glossary.md#simulation) steps exactly as it would with no clients. This sequence shows
one turn of a server's loop:

```mermaid
sequenceDiagram
    participant Game
    participant Server
    participant World as Simulation
    Game->>Server: Poll(wait)
    Server-->>Game: admissions and closings
    loop each admitted session
        Game->>Server: Receive(session)
        Server-->>Game: oldest waiting message
        Game->>World: push a command for the session's actor
    end
    Game->>World: step
    Game->>Server: SendSnapshots(step, view)
    Server->>Game: view(session), for each session due one
```

`Server::Poll` sends what is queued and takes in what has arrived: connections, hellos, messages
and closings. It waits up to `wait` for the first event and never longer; zero does not block.
Messages from admitted sessions go into each session's queue, and `Server::Receive` takes them
out oldest first. The game turns each request into a [command](glossary.md#command) for the
session's actor, so no request can name whom it acts for.

The client is the same: `Client::Poll` returns `ClientEvent`s (`Admitted`, `Message`,
`Snapshot`, `Closed`) in order, and `Client::Send` queues a message for the next poll.

### The handshake

A connection carries nothing but the offers and a hello until it is admitted. This diagram shows a connection's
life on the server:

```mermaid
stateDiagram-v2
    [*] --> Pending: ENet connects
    Pending --> Refused: anything but an offer (BadMessage)
    Pending --> Offered: offer, answered with the server's and its proof
    Offered --> Refused: a hello that does not open (KeyMismatch)
    Pending --> Refused: no hello in time (HandshakeTimeout)
    Offered --> Checking: hello
    Checking --> Refused: UnknownProtocol, VersionMismatch, ServerFull, BadCredential
    Checking --> Admitted: verifier says yes, welcome sent
    Admitted --> Closed: client closes or times out (Closed)
    Admitted --> Closed: engine kind or bad packet (BadMessage)
    Admitted --> Closed: queue full (InboundOverflow)
    Admitted --> Closed: Disconnect, with the game's reason
    Refused --> [*]
    Closed --> [*]
```

As soon as ENet connects, the client sends its *offer*, a fresh Curve25519 key, and the server
answers with one of its own. Each end then derives the connection's keys, one for each direction,
from two exchanges: the client's key with the server's own key, which the client pinned, and with
the server's fresh one. The first means only the holder of the server's secret can open what the
client seals; the second means a server's secret taken later opens no recorded connection, since
both fresh secrets are wiped once the keys are made.

The server's offer carries a *proof*: a message with no body sealed with the connection's keys,
which only the holder of the pinned secret can make. The client checks it before it sends anything
more, and closes as `KeyMismatch` if it does not open. Without it, a server that did not hold the
key could offer the pinned public key as its fresh one, and the hello it was sent would be sealed by
the server's secret alone, to be opened by whoever took that secret later.

Once the proof opens, the client sends its hello, sealed. It is a binary archive, format `jettison.hello` version 1,
of the protocol's name, its version and the credential, so the credential never crosses the network
in the clear. The hello's keys depend on the server's fresh key, so a hello recorded and replayed
on another connection does not open. The server checks, in this order:

1. that the hello opens, or the client pinned another key;
2. the protocol's name;
3. the version, against `ServerConfig::versions`;
4. that fewer than `maxSessions` are admitted;
5. the game's verifier, called once per session inside `Poll`.

Each refusal closes the connection with its reason, which ENet's disconnect carries, so the client
reads it in its `Closed` event. A refusal raises no `ServerEvent`: the
game only hears of sessions it admitted. Admission sends the welcome, and only then does either end
accept a game's message. A game checks that a client runs on the server's world content with its own
first message, as [files, packs and mods](files-and-mods.md#the-servers-world-content) sets out.

### The wire

A message is its kind, two bytes little-endian, then its body. A snapshot's body begins with the
step it was taken at, eight bytes little-endian. Every message is sealed: the packet is a counter,
eight bytes little-endian, then the message under XChaCha20-Poly1305 with the counter as its nonce
and the channel as additional data, 24 bytes more than the message. Only the two offers go
unsealed, each a kind and a 32-byte key. The server's is followed by its proof, a sealed message of
the offer's kind with no body, on the reliable channel at counter 0: 26 bytes, 60 for the whole
offer. Each end counts what it sends over both channels, and accepts on each channel only a counter
above the last it opened there, so a packet replayed, moved to the other channel, damaged or forged
is not opened. One that does not open closes an admitted session on the server (`BadMessage`) and is
dropped by a client. The largest counter is sealed once and then nothing more is, so neither count
wraps round to a nonce already used.

ENet's own headers, its sequence numbers, acknowledgements and closings, are not sealed. Someone
on the path can therefore close a connection, and the reason a closing gives is not proved to be
the other end's; they cannot read a message or make one that opens. Kinds from
`kFirstEngineKind` (`0xFF00`) up are the engine's own: the hello, the welcome, the snapshot and the
offer. Neither end sends a game's message
of one of those kinds (`Send` returns false), and a server closes a session that does
(`BadMessage`). Changing any of this breaks every client of an earlier build.

The handshake, requests and their results go `Reliable`; snapshots and a real-time game's inputs
go `Unreliable`, the newest superseding whatever is in flight. A snapshot longer than the MTU goes
in unreliable fragments, so a lost one is dropped whole rather than resent, holding back the next.

### Bounds

Every resource a client can make the server spend is bounded. These are `ServerConfig`'s fields
and `Limits`, which both ends have:

| Setting | Default | What it bounds |
| --- | --- | --- |
| `maxSessions` | 32 | Admitted sessions. A hello past it is `ServerFull`. |
| `maxPending` | 8 | Connections not yet admitted, on top of the sessions. Past both, ENet ignores a new connection, which times out at its client with `Closed`. |
| `handshakeTimeout` | 2 s | Time from connecting to the hello. |
| `maxInbound` | 256 | Messages a session may have waiting for `Receive`. One more closes it as `InboundOverflow`. |
| `snapshotInterval` | 100 ms | The least time between two snapshots to one session. |
| `Limits::maxMessageBytes` | 64 KiB | A whole message before it is sealed, kind and snapshot step included. |
| `Limits::timeout` | 5 s | Silence from a peer before it is closed. |

A message is one ENet packet, which ENet splits into datagrams and reassembles, so
`maxMessageBytes` bounds the whole message; the packet is the 24 bytes of its seal longer, and ENet's
limit is set to match. ENet drops a longer packet before it is reassembled
(its `maximumPacketSize`), and `Send` refuses to make one. A peer that sends an oversize reliable
packet therefore stalls its own reliable channel until it times out, rather than being closed with
a reason.

A reliable message of any length under the limit arrives, paced by ENet's window. An unreliable
message's fragments are all sent at once and must all arrive, so it must also fit the receiver's
socket buffer, the 256 KiB ENet asks for. On loopback 256 KiB arrived every time and 384 KiB never
(2026-11-24). A view larger than that wants deltas, which are not provisioned.

### Snapshots

`Server::SendSnapshots(step, view)` asks the view for each admitted session whose last snapshot
is at least `snapshotInterval` old, and sends the body on the unreliable channel with the step
first. So the view on the server decides what each session may see, at a rate bounded per session.
A view that returns `nullopt`, or one over the limit (logged), sends nothing, and the session is
asked again next time. The view may disconnect sessions as it
runs: the server walks the sessions by id and skips any that closed. On the client, a `Snapshot`
event holds the body in `message.body` and the step in `step`.

### Closing

| Call | What happens |
| --- | --- |
| `Server::Disconnect(session, reason)` | Sends what was queued, then closes with the reason. No `Closed` event follows for that session. |
| `Server::Closing()` | True while a session closed with `Disconnect` still has messages unacknowledged or its client has not answered. |
| `~Server` | Closes every connection at once as `Shutdown`, dropping what is queued. |
| `Client::Disconnect()` | Sends what was queued, then closes; the server sees `Closed`. |
| `~Client` | Closes at once. |

A server that must deliver its last messages disconnects each session, then polls until
`Closing()` is false or a deadline of its own passes, and only then is destroyed: polling, not a
blocking shutdown, because the game owns the loop.

### Keys

`Net::GenerateServerKey` makes a server's key pair, each key 64 lower-case hex characters, and
`Net::PublicKeyOf` gives the public half of a secret one. The server is given the secret
(`ServerConfig::secretKey`) and every client the public half (`ClientConfig::serverKey`); either
end refuses a key that is not 64 hex characters as `std::invalid_argument` when it is made.

The engine wipes the secrets it keeps once they are spent: the server's text of its secret key once
read, each fresh key once the connection's keys are made, the credential once the hello is sealed,
once it is judged and when a client is destroyed, and a connection's keys when it closes. A message
is written into its packet and sealed there, so its plain text is not copied. That narrows what a
later look at the process's memory could find; it does not close it. The hello is encoded by the
binary archive, whose working buffers, and any buffer a growing vector leaves behind, go back to
the allocator unwiped, and the engine does not lock its memory against swap. The caller's own
copies of its keys and credentials are its own. Where
the keys are kept and how a client is given its server's is the game's: a file the operator keeps
private, and the public half in the client's configuration or its build. A secret lost or leaked is
a new pair, and every client given the new public half, since a client connects only to the key it
pinned.

### Tokens

`Net::GenerateToken` gives a new bearer token from libsodium's random source. `Net::DigestToken`
gives its BLAKE2b-256 digest, also as hex. `Net::TokenMatches` compares a token's digest with a
stored one in constant time. A leaked store of digests gives away no token, and a token's 256 bits
of entropy make a fast digest enough where a password would want a slow one. The game issues, stores
and revokes tokens; revoking one closes its sessions with `Server::Disconnect(session,
Reason::Revoked)`.

## Using it

The [consumer project](sample-game.md#the-consumer-project), `source/test/consumer/main.cpp`, serves
a small race game over loopback. Its server holds a key its players pin, admits by token, and the verifier records the actor:

```cpp
const Net::ServerKey key = Net::GenerateServerKey();
const std::string hareToken = Net::GenerateToken();
const std::map<std::string, std::string> digests{{Net::DigestToken(hareToken), "hare"}};
std::map<Net::SessionId, std::string> actors;
Net::ServerConfig config;
config.protocol = "race";
config.versions = {kVersion};
config.maxInbound = 2;
config.secretKey = key.secretKey;
Net::Server server{
    config, [&](Net::SessionId session, std::span<const std::byte> credential)
    {
	    const std::string token{
	        reinterpret_cast<const char *>(credential.data()), credential.size()};
	    for (const auto &[digest, piece] : digests)
		    if (Net::TokenMatches(token, digest))
		    {
			    actors[session] = piece;
			    return true;
		    }
	    return false;
    }};
```

A client names the port, the protocol, the server's public key and its credential, then polls
until it is admitted or closed. `port` 0 on the server took an ephemeral one, which `Server::Port` reports:

```cpp
Net::ClientConfig player;
player.port = server.Port();
player.protocol = {"race", kVersion};
player.serverKey = key.publicKey;
player.credential = Bytes(hareToken);
Net::Client client{player};
// Poll both ends until client.Admitted(), or a Closed event gives the reason it was refused.
```

A request becomes a command for the session's actor; it names no piece, so a client cannot act
for another:

```cpp
const std::optional<Net::Message> request = server.Receive(session);
if (request && request->kind == kPlaceMine)
	world.Commands().Push(Place{actors.at(session)});
```

After a step, each session is sent only what its actor may see, encoded in the binary archive:

```cpp
server.SendSnapshots(
    world.Clock().Step(),
    [&](Net::SessionId viewer) -> std::optional<std::vector<std::byte>>
    {
	    std::vector<Token> visible;
	    for (auto [entity, token] : world.Registry().view<Token>().each())
		    if (token.piece == actors.at(viewer))
			    visible.push_back(token);
	    Serialise::BinaryWriter writer{kViewFormat};
	    writer.Write(visible);
	    return writer.Finish({1});
    });
```

A clean stop delivers its last message before closing, from
`source/engine/net/tests/NetTests.cpp` (`Until` polls both ends until the condition holds):

```cpp
CHECK(fixture.pServer->Send(session, Channel::Reliable, 9, Bytes("last")));
fixture.pServer->Disconnect(session, Reason::Shutdown);
CHECK(fixture.pServer->Closing());
fixture.Until([&] { return !fixture.pServer->Closing(); }, {&client});
CHECK(fixture.ClosedWith(client) == Reason::Shutdown);
```

Manifest's dedicated server and remote client are a full example: `Manifest::Remote::Server`
(Manifest's `source/game/remote/include/manifest/remote/Server.h`) reads its tokens from a file,
closes revoked sessions as `Revoked`, and stops through `Closing`. Its contract is [Manifest's
network contract](../../manifest/docs/server.md).

## Limits and open work

- ENet's headers are not sealed, so someone on the path can close a connection, with any reason
  (above).
- Secrets are wiped where the engine keeps them, not in the serialiser's or the allocator's spent
  buffers (above).
- One key a server, pinned: changing it means giving every client the new one. A long-lived key that
  signs the server's working keys is not provisioned.
- No IPv6: ENet 1.3 does not have it.
- No deltas against an acknowledged snapshot, so an unreliable view must fit in 256 KiB (above).
- A real-time game's inputs at step rate need nothing new until a game measures them.
- A peer's oversize packet stalls it to a timeout (above).

## Decisions

The settled choices are in [decisions](decisions.md#networking); the scope's verdicts in
[scope](scope.md#networking).

## Source

- `source/engine/net`: the module, which depends on `core` and `serialise` and links ENet
  and libsodium privately.
  - `include/jettison/net/Protocol.h`: `Channel`, `Reason`, `Protocol`, `MessageKind`,
    `Message`, `Limits`.
  - `include/jettison/net/Server.h`: `Server`, `ServerConfig`, `ServerEvent`, `Verifier`,
    `SnapshotView`.
  - `include/jettison/net/Client.h`: `Client`, `ClientConfig`, `ClientEvent`.
  - `include/jettison/net/Key.h`: `ServerKey`, `GenerateServerKey`, `PublicKeyOf`.
  - `include/jettison/net/Token.h`: `GenerateToken`, `DigestToken`, `TokenMatches`.
  - `src/Seal.h`: the key exchange and the sealed packet, on libsodium.
  - `src/Wire.h`: the packet layout, the offers and the hello's encoding.
- `source/engine/net/tests/NetTests.cpp`: the `net` suite over loopback, including a raw ENet
  peer that is not the engine's client.
- `source/test/consumer/main.cpp`: the independent consumer's served race.
