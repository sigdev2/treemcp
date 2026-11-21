# Internals

The stdio server reads native Windows handles or POSIX file descriptors on a
dedicated thread. It hands one bounded chunk at a time to the Qt event loop,
which decodes records, dispatches requests and flushes replies. Search requests
share a bounded worker pool. A batch coordinator submits opened files as workers
become available.

- [Request lifecycle](lifecycle.md)
- [Batch execution](batch.md)
- [Security and resource limits](security.md)
- [Architecture decisions](../adr/README.md)

```text
stdin -> bounded JSON-RPC decoder -> dispatcher -> admission -> bounded scheduler
                                                                    |
                                                              worker pool
                                                                    |
stdout <- one main-thread serializer <- bounded completion queue <---+
```

Protocol/control messages remain responsive in the main event-loop thread.
Workers own their Tree-sitter parser and query cursor. A worker reads each
selected file and owns its buffer and trees. The serializer is the only stdout writer.

Separate Windows and POSIX transport and secure-open implementations are required.
Do not assume POSIX descriptor semantics for Windows stdin or Windows filesystem handles.
