# Request-local state

Status: accepted.

Never retain workspace-derived sources, ASTs, compiled queries, listings, results
or indexes between requests. Do not create persistence, logs or temporary state files.
Each request observes fresh, coherent per-file reads rather than a stored repository snapshot.

One batch may share a source buffer and one tree per grammar for a file during
that request. All such state is released before the request is finished or cancelled.
Immutable schemas, static configuration and compiled language metadata may remain.

This sacrifices incremental parsing across requests in exchange for explicit
freshness and a smaller retained-data surface. Operating-system page caching is
outside the application cache policy.
