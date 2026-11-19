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
