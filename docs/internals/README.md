# Internals

The stdio server reads native Windows handles or POSIX file descriptors on a
dedicated thread. It hands one bounded chunk at a time to the Qt event loop,
which decodes records, dispatches requests and flushes replies. Single-file
queries run synchronously with a request-local parser, tree, query and cursor.
A batch coordinator submits a bounded number of file jobs to its own workers.

- [Request lifecycle](lifecycle.md)
- [Batch execution](batch.md)
- [Security and resource limits](security.md)
- [Architecture decisions](../adr/README.md)

The planned general scheduler extends this batch-specific scheduling model.

```text
stdin -> bounded JSON-RPC decoder -> dispatcher -> admission -> bounded scheduler
                                                                    |
                                                              worker pool
                                                                    |
stdout <- one main-thread serializer <- bounded completion queue <---+
```

Protocol/control messages remain responsive in the main event-loop thread.
Workers own their Tree-sitter parser and query cursor. A worker receives one coherent file buffer and owns its trees. The serializer is the only stdout writer.

Separate Windows and POSIX transport and secure-open implementations are required.
Do not assume POSIX descriptor semantics for Windows stdin or Windows filesystem handles.
