# MCP protocol compatibility

TreeMCP targets **2025-11-25** over stdio. Clients launch `structural-mcp` with
no arguments. Changing the pin requires updating the handshake, wire tests and
this record together. [Published specification](https://modelcontextprotocol.io/specification/2025-11-25)

## Session contract

1. Send `initialize` with a protocol version string, capabilities object and
   client information (name and version). The server returns its build identity,
   the pinned revision and the static `tools` capability. A client proposing a
   different revision receives the pin and should disconnect if it cannot use it.
2. Send `notifications/initialized` before discovering or calling tools. A ping
   is accepted throughout initialization. Initialization can succeed once per process.
3. Use a fresh string or integer ID for every request in the session, including
   failed requests. Notifications have no ID and receive no reply.
4. Close stdin to finish the session. Each direction carries one UTF-8 JSON
   object per LF-terminated record. CLI diagnostics use stderr.

See the official [lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle),
[message types](https://modelcontextprotocol.io/specification/2025-11-25/basic)
and [stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

## Wire limits and errors

| Limit | Behavior |
| --- | --- |
| 1 MiB before LF | Accept LF or CRLF. Discard an oversized record through its delimiter and resume with the next record. The optional CR counts toward the limit. |
| 64 levels below the root, 1,024 members per object | Reject deeper values, wider objects and duplicate member names. |
| Request ID | Accept signed 64-bit integer literals and strings up to 1,024 UTF-8 bytes. Numeric and string IDs are distinct. |
| 16,384 admitted request IDs per process | At capacity, new requests receive `-32001`. Start a new session to continue. |

Malformed UTF-8 or JSON, blank records and an unfinished record at EOF produce
`-32700`. Invalid envelopes, batch arrays and reused IDs produce `-32600`.
An error omits `id` when it cannot be identified. Unknown methods receive
`-32601`, malformed parameters receive `-32602`, and tools called before the
handshake completes receive `-32002`. The server issues no client requests.
Unknown notifications and unsolicited well-formed responses are ignored.

## Tool contract

The initial catalog exposes the immutable grammar registry through `languages`.
Discovery and execution use `tools/list` and `tools/call`. A successful call
returns the output object in `structuredContent` and its compact JSON in text
content. Unknown tools and malformed calls return protocol errors. Arguments
that violate a known tool's input schema produce a tool result with `isError`.
[Official tool contract](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)

The foundation handles requests synchronously. A cancellation arriving after
completion is ignored. Worker cancellation and shutdown with active searches
belong to the scheduler milestone. Client-specific compatibility requires a
separate test against the named client and version.
[Cancellation](https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/cancellation)

The [verification record](../development/testing.md#protocol-foundation-verification)
identifies the tested build, wire tests and SDK client.
