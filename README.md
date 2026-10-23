# Android proximity sensor

**Native C proximity reader**, paired with the [existing accelerometer instrument](https://github.com/Ashtray-Archer/utilities-android-phone-user/tree/main/accelerometer).

Targets: MIRO C67 (`arm64-v8a`, Android 14) and MIRO A1 (`armeabi-v7a`, Android 14). The C67's `dumpsys sensorservice` advertises an MTK `android.sensor.proximity` on-change wake-up sensor, with historical readings `1.00`. The A1 is not yet proven to have this sensor.

## Reader

- Large live **NEAR**, **FAR**, or **WAITING** state, with readable contrasting text.
- Actual value delivered by the Android sensor stack (cm), not a claimed precision distance.
- Sensor-provided maximum range, name, and vendor.
- Count and last eight state transitions, timestamped relative to the first event. Identical near/far states don't fill the history.
- Explicit error or missing-sensor states.
- Automatically suspend the sensor on lost focus; no recording in the background.

`ASensorEvent.distance < ASensor_getMaxRange(sensor)` is **NEAR**, and a value at or above the maximum is **FAR**. Many phone sensors give only two readings, 0 (near) or maxRange (far). A report of `1.00` without knowing the reported maximum range is not automatically a measured 1 cm distance.

This is the Android NDK sensor interface (`ASensorManager`/`ASensorEventQueue`), **not** raw optical data or a direct vendor HAL driver. An actual sensor may be optical or virtual; this API alone cannot determine which.

## Build and APKs

NativeActivity APK, no Java/Kotlin, `classes.dex`, Gradle, network permission, microphone permission, or Termux:API companion.

Requires Android SDK platform 36, build tools 36.0.0, NDK 27.2.12479018, `zip`, Java `keytool`, and a stable Android signing keystore. The script rejects missing or mismatched signing certificates:

```sh
ANDROID_HOME=/path/to/sdk \
ANDROID_KEYSTORE=/path/to/wegert-debug.keystore \
sh build-apk.sh
```

Signed, device-specific packages:

- `dist/proximity-c67-arm64-v8a.apk`
- `dist/proximity-a1-armeabi-v7a.apk`

The [native APK workflow](../../actions/workflows/proximity-apk.yml) retains both APKs and SHA-256 hashes as a build artifact. Install the C67 package through Android's file installer, or using Shizuku once its contents are accessible to the shell. It doesn't replace the Accelerometer app.

Host model test:

```sh
cc -std=c17 -Wall -Wextra -Werror -pedantic -I. tests/model_test.c -lm -o /tmp/proximity-test
/tmp/proximity-test
```

Physical C67 acceptance requires installing the artifact and moving a hand over the **top edge** of the phone. No physical-device verification is claimed before that test.
