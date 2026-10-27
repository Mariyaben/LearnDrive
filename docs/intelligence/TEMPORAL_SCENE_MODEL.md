# Temporal scene model

The existing ten-second ShortMemory and ledger remain bounded. The new scalar
interpreter retains64 existing actor identities, including namespace and scope.
It is neither object re-identification across arbitrary cuts nor a trajectory
prediction model. Source changes clear contextual evidence.

| Phase | Evidence |
|---|---|
| OBSERVED | First fresh observation of this identity |
| CONTINUING | Another distinct capture, including recovery from a brief loss |
| POTENTIALLY_RELEVANT | Relevant region, at least3 distinct observations spanning0.25s, not yet addressed |
| CHANGING | Material relevance/path/closing change or substantially reduced optical TTI |
| TEMPORARILY_LOST | No fresh observation for0.35s; uncertainty, never a clear-road claim |
| RESOLVED | Fresh supported exit from the relevant image region |
| EXPIRED | Episode older than10s without renewed evidence |

Repeated100Hz rule ticks on one capture cannot manufacture continuity. The first
observation has zero image delta, not a fictitious motion from the image origin.
Image delta is normalized image displacement, not meters, velocity or proof of
parallel/crossing motion. `supported_path=false` means unknown unless a producer
has independently established stronger evidence. Current actionable actors and
fresh relevant tracks precede older lost tracks in the at-most12 encoded episodes;
compact inference reads at most8 of them.

Route context records maneuver identity, route/epoch, distance, ETA, GPS/capture
freshness and150/70/20m preparation windows. These are experimental windows, not
validated learner timing. Provider navigation has a separate freshness budget.
Provider context blocks learned evaluation, and provider-derived history remains
excluded even after a switch back to offline navigation.

Measured speed history yields unknown/steady/increasing/decreasing. The response
field is uncertain unless current speed is valid and a completed or intentionally
visual cue was followed by a measurable trend within the bounded interval.
It does not establish which control the driver used, a causal response to the
instruction, or that the response was appropriate. No speed is fabricated for
recorded video without independently supplied telemetry.

Generation changes invalidate cached reasoning on meaningful phases, supported
path/closing changes, candidate-set changes/expiry, route windows, source/settings,
freshness, speed trend or delivery changes. Candidate timestamps alone do not
cause a model invocation every frame. Busy traffic can still change generation
frequently; invocation throttling and a single worker remain necessary.

Native policy adds semantic-equivalent delivered-cue suppression across producer
wording, continuity checks for unqualified first observations, uncertainty silence
and observed-response silence. It preserves original evidence validation, native
urgent priority and experimental20/12/8s routine pacing. Silence reason codes are
diagnostics; they are not evidence that a human instructor would prefer silence.

Delivery stages are selected, scheduled, submitted, started, interrupted, expired,
failed, visual-only and completed. Voice-on submission failure consumes neither
the encounter acknowledgement nor its routine interval. Failed/interrupted/expired
delivery can be retried with new fresh evidence. Completed PCM playback does not
prove the driver heard or understood it. Muting intentionally accepts visual cues.
