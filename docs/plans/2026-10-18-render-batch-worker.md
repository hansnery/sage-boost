# Render-batch worker experiment

Execute inline under the user's continuing development authorization.

- [x] Build an optional standalone fixture with one GPU-owning worker,
  two bounded copied-palette slots and explicit completion/sequence checks.
- [x] Compare unqueued references with published packets; test producer
  scratch overwrite, stale pose, changed material and backend restoration.
- [x] Run sequential lit/shadow comparisons of direct, immediate-wait copied
  and pipelined copied modes. Retain checked/owned timing modes separately;
  include copies, backpressure and GPU completion on every path.
- [x] Preserve input hashes and paired samples, review the actual next
  integration boundary, update STATUS/BACKLOG and commit/push the evidence.

The design and limits are in
`docs/specs/2026-10-18-render-batch-worker.md`. No external DLL is needed and
no runtime plugin or installed setting is changed by this experiment.
