# Android proximity sensor

A small native Android instrument copied and adapted from the existing
[Accelerometer](https://github.com/Ashtray-Archer/utilities-android-phone-user/tree/34b2391a97867ab25ea885cedc7ac2e04b591609/accelerometer)
project. This is a standalone repository, not a new Java/Kotlin/Gradle application.

## What it does

- Displays Android's reported distance in centimeters. Once two distinct
  levels have been observed, it estimates **NEAR** / **FAR** and shows the
  highest observed distance; it does not claim that number is the sensor's
  declared maximum range.
- Counts Android sensor events and logs raw distance, timestamp, and state to
  Android logcat under the tag `Proximity`.
- Retains the Accelerometer's DEX-free `android.app.NativeActivity`,
  native-window framebuffer drawing, antialiased bitmap text, and ARMv7 Thumb
  packaging, with its own icon and package identity.
- Builds `armeabi-v7a`, `arm64-v8a`, `x86_64`, and universal APKs.
  Also builds Android command-line binaries and optionally an Android App Bundle.

The sensor type is Android `ASENSOR_TYPE_PROXIMITY` (8). Unlike the three-axis
accelerometer, this is an **on-change**, single-value sensor. A typical binary
proximity sensor reports 0 when covered and the maximum range when uncovered.
The Android NDK's public SensorManager API does **not** expose
`Sensor.getMaximumRange()`, so this native-only app must not invent that value.
Initially it displays WAIT while learning observed levels. After covering and
uncovering the sensor produces two distinct readings, the lower values are
tentatively NEAR and the highest observed value is FAR. That follows the usual
binary sensor convention, but for a continuously varying sensor it is only a
relative estimate. The live numeric value remains the actual Android reading.

No special Android permission is needed. The app listens while foregrounded
and unregisters on pause/loss of focus. A wake-up sensor does **not** mean this
app has implemented screen-off/background capture.

## Source layout

| Path | Role |
| --- | --- |
| `android/android_proximity.[ch]` | Android NDK SensorManager boundary and timestamped distance |
| `model/proximity_model.h` | Near/far classification, event and transition counts, input validation |
| `app/src/main/c/native_main.c` | Native activity with adapted Accelerometer framebuffer glyph renderer |
| `command/proximity.c` | Native command-line reader, as with Accelerometer hardware paths |
| `tests/proximity_model_test.c` | Host tests, independent of a phone |
| `build-apk.sh` | Signed, DEX-free APK and command-line executable packages |
| `build-bundle.sh` | Optional `.aab` distribution |

The accelerometer's balanced-gravity subtraction, compact octahedral state,
x/y/z values, and seventh-based numeric presentation do not belong to this
sensor and were intentionally not copied.

## Build and test

```sh
sh tests/run-host.sh
```

For APKs use Linux x86-64 with Android SDK platform 36, build-tools 36.0.0,
NDK 27.2.12479018, and standard `zip`, `unzip`, `keytool` tools:

```sh
ANDROID_HOME=/path/to/android-sdk \
ANDROID_KEYSTORE=/path/to/pinned-test-keystore \
sh build-apk.sh
```

A persistent signer is mandatory. The expected test certificate SHA-256 is
`DE:9B:1D:47:C5:A6:5E:6D:46:A2:04:B7:9D:D9:EE:56:6B:9D:3C:98:32:BA:81:EB:C4:21:3D:33:92:E9:2F:F9`.
The CI job retrieves that public test keystore from a pinned Wegert revision.
Do not use the public test signer for private or store identities.

Outputs:

```text
app/build/outputs/apk/debug/app-debug.apk
app/build/outputs/apk/distribution/proximity-armeabi-v7a.apk
app/build/outputs/apk/distribution/proximity-arm64-v8a.apk
app/build/outputs/apk/distribution/proximity-x86_64.apk
app/build/outputs/apk/distribution/proximity-universal.apk
app/build/outputs/apk/distribution/proximity-armeabi-v7a
app/build/outputs/apk/distribution/proximity-arm64-v8a
app/build/outputs/apk/distribution/proximity-x86_64
```

On the MIRO A1 choose `armeabi-v7a`. On the MIRO C67 choose the ABI
reported by `getprop ro.product.cpu.abi` (typically `arm64-v8a`).

To generate an Android App Bundle, first build the APK, then provide
`BUNDLE_AAPT2` and `BUNDLETOOL_JAR` and run `sh build-bundle.sh`.
This produces `app/build/outputs/bundle/distribution/proximity.aab`.

The app itself contains no `classes.dex`. Android framework
`NativeActivity` still provides normal Android app lifecycle integration.

### Command-line interface

Copy the ABI-appropriate executable to the phone, `chmod 755` it, then run:

```sh
./proximity-armeabi-v7a read /hardware/sensors/proximity/info
./proximity-armeabi-v7a read /hardware/sensors/proximity/sample
./proximity-armeabi-v7a read /hardware/sensors/proximity/events 10
./proximity-armeabi-v7a read /hardware/sensors/proximity/inspect
./proximity-armeabi-v7a read /hardware/sensors/proximity/inspect-events 10
```

Event streams use tab-separated `timestamp_ns\tdistance_cm\tstate`.
Inspection uses stable dotted TSV keys and includes state transitions.
Because the sensor is on-change, a request for 10 events may not finish until
something moves across the sensor.

### Physical acceptance still needed

The host tests prove only classification and retained-state behavior.
For each APK and device, record SHA-256, ABI, signer, installation, app launch,
observed near/far changes on covering the earpiece sensor, reported distance and
observed high level, and logcat timestamps. Confirm the screen changes back to FAR
when uncovered. A wake-up flag in `dumpsys sensorservice` is *not* evidence that
the app is monitoring while suspended.

## Provenance

The acquisition adapter, native framebuffer text renderer, manifest structure,
and APK/AAB packaging derive from
[Ashtray-Archer/utilities-android-phone-user/accelerometer at 34b2391a](https://github.com/Ashtray-Archer/utilities-android-phone-user/tree/34b2391a97867ab25ea885cedc7ac2e04b591609/accelerometer).
The standalone proximity model and command output are adaptations. Android
acquisition still uses C through the NDK; this does **not** establish that
Idriç or ICK generated the native object. The Accelerometer's latest ICK
source uses the `÷` token; this standalone NDK C version uses C's `/`
operator instead, without asserting equivalence of compiler pipelines.
