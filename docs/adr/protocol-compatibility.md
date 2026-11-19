# MCP protocol compatibility policy

Status: open. The bootstrap implements no MCP handshake and claims no supported
protocol revision. This page defines the work needed to make that claim.

## Selecting a revision

Before implementing the transport, select an official MCP revision and record its
exact identifier, a link to its published specification and the conformance tests
used to validate support. Do not infer support from a client configuration example
or from the presence of JSON-RPC messages alone.

## Required implementation record

Document the behavior for the selected revision:

- Initialization messages, their order, version negotiation and unsupported versions.
- Advertised capabilities and the methods and notifications they enable.
- Stdio message framing, request IDs, errors and malformed input handling.
- Tool discovery, input/output schemas and structured/text result representations.
- Cancellation, outstanding requests and process shutdown.

The record must distinguish implemented behavior from optional or deferred features.
Only advertise capabilities and tools supported by the executable.

## Compatibility evidence

Pass the [protocol acceptance checks](../development/acceptance.md#protocol) and
record the server build, selected revision, client versions and test results.
For OpenCode, also pass the [client integration checks](../development/acceptance.md#opencode-integration)
with the exact configuration and mode documented for users.

A change to the supported revision or handshake requires updated documentation,
configuration examples and compatibility tests before it is released. Publish the
supported revisions and any migration requirements in the release notes.
