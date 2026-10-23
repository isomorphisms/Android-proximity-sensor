#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$SDK" ]; then
    echo "Set ANDROID_HOME (Android SDK)." >&2
    exit 2
fi
NDK=${ANDROID_NDK_ROOT:-"$SDK/ndk/27.2.12479018"}
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
GLUE="$NDK/sources/android/native_app_glue"
TOOLS="$SDK/build-tools/36.0.0"
ANDROID_JAR="$SDK/platforms/android-36/android.jar"
SIGNER=${ANDROID_KEYSTORE:-}
STORE_PASSWORD=${ANDROID_KEYSTORE_PASSWORD:-wegert-debug}
KEY_PASSWORD=${ANDROID_KEY_PASSWORD:-$STORE_PASSWORD}
KEY_ALIAS=${ANDROID_KEY_ALIAS:-wegert-debug}
EXPECTED=${ANDROID_EXPECTED_CERT_SHA256:-de9b1d47c5a65e6d46a204b79dd9ee566b9d3c9832ba81ebc4213d3392e92ff9}

for file in "$GLUE/android_native_app_glue.c" "$TOOLS/aapt2" "$TOOLS/zipalign" "$TOOLS/apksigner" "$ANDROID_JAR"; do
    if [ ! -e "$file" ]; then
        echo "Missing Android tool: $file" >&2
        exit 2
    fi
done
if [ -z "$SIGNER" ] || [ ! -f "$SIGNER" ]; then
    echo "ANDROID_KEYSTORE must name an existing stable signing key" >&2
    exit 2
fi
ACTUAL=$(keytool -list -v -keystore "$SIGNER" -storepass "$STORE_PASSWORD" \
    -alias "$KEY_ALIAS" 2>/dev/null | sed -n 's/^[[:space:]]*SHA256: //p' |
    head -n 1 | tr '[:upper:]' '[:lower:]' | tr -d ':')
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "Refusing unexpected signer: $ACTUAL" >&2
    exit 2
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/proximity-apk.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$ROOT/dist"
"$TOOLS/aapt2" link \
    -I "$ANDROID_JAR" \
    --manifest "$ROOT/app/src/main/AndroidManifest.xml" \
    --min-sdk-version 26 --target-sdk-version 34 \
    --version-code 1 --version-name 0.1.0 \
    -o "$WORK/base.apk"

build_one() {
    ABI=$1
    case "$ABI" in
        arm64-v8a)
            CC="$BIN/aarch64-linux-android26-clang"
            set --
            OUT_NAME=proximity-c67-arm64-v8a.apk
            ;;
        armeabi-v7a)
            CC="$BIN/armv7a-linux-androideabi26-clang"
            set -- -mthumb -march=armv7-a -mfloat-abi=softfp
            OUT_NAME=proximity-a1-armeabi-v7a.apk
            ;;
        *)
            echo "Unsupported ABI: $ABI" >&2
            exit 2
            ;;
    esac
    test -x "$CC"
    mkdir -p "$WORK/$ABI/lib/$ABI"
    "$CC" "$@" -std=c17 -O2 -DNDEBUG -fPIC -fstack-protector-strong \
        -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Wpedantic \
        -Wno-strict-prototypes -Wno-deprecated-declarations -Wno-unused-parameter \
        -I"$ROOT" -I"$GLUE" \
        "$ROOT/app/src/main/c/native_main.c" \
        "$GLUE/android_native_app_glue.c" \
        -shared -Wl,-u,ANativeActivity_onCreate -Wl,--gc-sections \
        -Wl,-z,relro,-z,now -landroid -llog -lm \
        -o "$WORK/$ABI/lib/$ABI/libproximity.so"
    "$BIN/llvm-strip" --strip-unneeded "$WORK/$ABI/lib/$ABI/libproximity.so"

    cp "$WORK/base.apk" "$WORK/$ABI/unsigned.apk"
    (cd "$WORK/$ABI" && zip -0 -q -r unsigned.apk lib)
    "$TOOLS/zipalign" -f -P 16 4 "$WORK/$ABI/unsigned.apk" "$WORK/$ABI/aligned.apk"
    "$TOOLS/apksigner" sign --ks "$SIGNER" --ks-key-alias "$KEY_ALIAS" \
        --ks-pass "pass:$STORE_PASSWORD" --key-pass "pass:$KEY_PASSWORD" \
        --out "$ROOT/dist/$OUT_NAME" "$WORK/$ABI/aligned.apk"
    "$TOOLS/apksigner" verify --verbose --print-certs "$ROOT/dist/$OUT_NAME"
    unzip -Z1 "$ROOT/dist/$OUT_NAME" | grep -qx "lib/$ABI/libproximity.so"
    if unzip -Z1 "$ROOT/dist/$OUT_NAME" | grep -qx 'classes.dex'; then
        echo "Unexpected classes.dex" >&2
        exit 1
    fi
}

build_one arm64-v8a
build_one armeabi-v7a
(cd "$ROOT/dist" && sha256sum proximity-*.apk > SHA256SUMS)
cat "$ROOT/dist/SHA256SUMS"
