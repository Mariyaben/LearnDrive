# Privacy inventory for the learner release

Source review: 22 October 2026. This is an engineering inventory for preparing
the public notice and store disclosures, not a published privacy policy. The
business identity, support contact, public URLs and final retention commitments
must be supplied before publication.

| Data / feature | Current app behavior | Release work |
| --- | --- | --- |
| Camera | Foreground frames are processed on the device by native/ONNX perception; no LearnDrive camera upload service is implemented | Verify the signed build's traffic and SDK behavior; clearly explain permission and purpose |
| Motion and location | Phone sensors and location support mount checks, speed and route context; unsupported/missing measurements remain unavailable | Explain approximate/precise location and permitted purpose; verify permission denial and foreground lifecycle |
| Google search/navigation | Typed queries, selected destinations, location and SDK requests involve Google services and require internet | Link applicable Google terms/privacy, inventory actual SDK collection and preserve required attribution |
| Offline navigation | Optional regional pack and local routing remain a separate provider inside the same dashboard | Review pack/data attribution, redistribution rights and version/update behavior |
| Session review | Bounded in-memory counts of accepted cues and sampled camera freshness; no account, cloud review or driver score | Explain temporary retention; do not imply saved review history or confirmed learner actions |
| Diagnostic capture | Separate opt-in JSONL capture in app-private storage, bounded at 8 MiB; explicit export/delete controls | Explain motion, timestamps, cue/source/availability fields; export is the user's chosen destination and may outlive app deletion |
| Navigation diagnostics | Restricted provider speed-limit content is redacted from retained JSON; live policy inputs are separate | Keep provider-content retention tests and review future diagnostic additions |
| Development logs | Debug builds emit structured guidance logs through Android logging; these are not a production analytics backend | Decide production log policy and inspect release artifact before distribution |
| Local settings | Navigation-provider choice and app preferences are kept locally | Document clearing/uninstall behavior; do not promise cloud synchronization |
| Payments/accounts | No subscription, entitlement-verification service or LearnDrive account backend is implemented | Inventory store/purchase/support data when payment is added, before claiming those flows are available |

No public promise of “all data stays on the phone” is appropriate for Google
navigation mode. Conversely, using Google navigation does not mean this app
uploads camera video to a LearnDrive server. Describe each actual data path.

Before release, verify the merged manifest and a signed-build network capture,
complete the SDK inventory and store data-safety form, publish accurate terms
and privacy documents, and make them accessible from the app/store listing.
Do not copy a generic policy that claims implemented accounts, cloud deletion,
retention periods or analytics controls that this application does not have.

Google requires public terms/privacy for Places integrations and specifies
attribution and content-storage restrictions in its
[Places policies](https://developers.google.com/maps/documentation/places/android-sdk/policies).
Navigation has additional
[implementation and retained-content rules](https://developers.google.com/maps/documentation/navigation/android-sdk/policies).
