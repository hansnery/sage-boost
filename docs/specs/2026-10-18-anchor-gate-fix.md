# Correct skeletal anchor qualification after the beta.10 gameplay test

The user measured a maximum of 30 FPS with FrameView in the troop-heavy
save. The copied beta.10 capture confirms 1,769 completed flip-enabled
EndScene calls in seconds 50 through 109: **29.4833/s**, with no extra
frames. Both pooled median and p95 gaps are in `(33.334, 50] ms`.
QueueInsert, QueueSort and BulkRender all activated. No verification failure
was reported. This is a failed gameplay result, not a near-60 success.

The interval averages about 285 instanced meshes and 7,127 mismatches per
game frame; the previous beta.2 capture averaged 6,837 and zero respectively.
The runs are not identical pose/camera samples, but the rejection increase
exposes an eligibility problem worth reproducing before scheduler changes.
Cost blocked 1,477 opportunities and catch-up blocked 272. The extra-cost
estimate remained 17.714 ms while average available idle time was 8.155 ms.
No recovery probes ran. The scope costs overlap and are not additive.

[Gameplay evidence](../benchmarks/2026-10-18-beta10-gameplay-check.json)
preserves the selected interval, counts, activation lines and capture hashes.

## Native branch and error

The complete verified mesh body reads +0x310 as a pointer to an anchor
pointer. For skeletal meshes, null storage, a null pointee and a pointee
equal to the mesh all select the same identity-world branch. A different
anchor invokes the relative-transform branch. Both installed executable
families contain the same 2,482-byte mesh body, SHA256
`4a19fe1576dbff6187d3d080c3a02c5bafa95180526cf460843da5f213511260`.
This body is already covered by the runtime bulk contract; no signatures,
patch sites or executable addresses change.

Beta.10 rejects any non-null +0x310 value, including both supported cases,
at leader/member admission and bulk qualification. It also reports these
as model changes. The original GPU fixture used only null storage, so it
did not expose this distinction.

## Beta.11 correction

One general-register helper follows the native identity-world condition.
Leader/member admission and all three bulk gates use it; rigid admission
does not read the skeletal anchor field. A different skeletal anchor stays
native, flushing pending work before its material state changes. Its log
reason is now `relative skin anchor`, separate from a model change.

The runtime GPU fixtures now mix null storage, null pointees and self
anchors across all 7,200 meshes, including leaders and followers. They
execute the real native branch and production hooks in regular and fresh
shadow frames. Existing ordering, reference, fallback and failure checks
apply to these workloads.

The new admission test and complete native/GPU fixture both fail with the
old predicate. With the correction, the admission/ownership test passes
19 cases with zero dead-object uses, including pending-work cleanup for a
different anchor. Both executable families pass the expanded native D3D9
and DXVK tests: eight combined queue/GPU workloads, 384 fallback cases,
496 injected single-GPU failures, 56 deliberately broken device-effect
controls and 24 broken-record controls. Regular and extra-frame lit/shadow
output remains within the existing pixel/depth tolerances.

The full plugin suite passes, including native hook installation and seven
group-lifetime cases with actual Windows unwind. Inspection of the final
production hook/core objects finds no x87, SIMD or FP-control instructions.
One validation attempt was refused because the game had reopened; after
the user closed it, the remaining checks ran serially and passed.

Six serial DXVK runs retain 126 samples: four warmups, 21 alternating AB/BA
samples per run and eight separately fenced regular/extra pairs per sample.
With the mixed anchors, median completion savings versus the ordinary
instancing baseline are **8.18-8.66 ms** per pair for shuffled input and
**6.52-7.14 ms** for ordered input. These measure the combined insertion,
sorting and production group wrapper/bridge/instancer path. They are not
additional savings to add to beta.10's figures or to individual stage costs.
The fixture still has test-only submission observers absent from the ASI.

[Validation and timing manifest](../benchmarks/2026-10-18-anchor-gate-checks.json)
preserves distributions, source/build/log hashes and the refused attempt.
[All 126 samples](../benchmarks/2026-10-18-anchor-gate-samples.csv) preserve
regular, extra and fenced-completion times. Tested beta.11 ASI SHA256:
`574a085967d477a53037526c1ea7f6b6d257a559a36aab7f76573c4ca7cfe027`.

This restores eligibility for native identity cases; it does not prove how
many live meshes use each anchor case. No scheduler margin, scene quality,
simulation, input, network, camera, FP controls or installed INI value is
changed. Actual 60 FPS remains unproved and requires the corrected build's
saved-game capture after installation.

Beta.11 was committed/pushed as `7d17e24` and installed at
2026-10-18T09:35:16 local time with the game closed. The installed hash
matches the tested ASI above. Beta.10, its INI and its capture were backed
up first; the INI stayed byte-for-byte unchanged. The
[installation receipt](../benchmarks/2026-10-18-anchor-gate-install.json)
records all hashes and the backup location. The next game run loads beta.11;
no game was launched by the agent.
