# Charcoal presentation

The native instrument uses the fixed background, primary text, secondary text
and divider roles from `isomorphisms/android-NDK` at
`3e682fb1175beef5625dbd2418730220e49fc9bd`.
Check that source out at `.charcoal` and stage
`.charcoal/native/presentation/res/.` into `app/src/main/res/` before the existing
APK or bundle builder. The package workflow performs both steps.

`Theme.Charcoal` explicitly controls the launch window, splash, system bars and
icon appearance independently of Android's system night setting. The activity
retains its existing non-fullscreen layout, continuous Subpixel text, package,
version and persistent signing identity. The existing NEAR/FAR indicator colors
and sensor model are preserved; their labels also convey state.

API 29/34 visual and functional execution, rotation, both system night settings
and physical MIRO C67 acceptance must be recorded separately from compilation.
No physical acceptance is established by this change.
