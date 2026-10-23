#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$sdk_root" ]; then
    echo "ANDROID_HOME or ANDROID_SDK_ROOT is required" >&2
    exit 2
fi

bundle_aapt2=${BUNDLE_AAPT2:-}
bundletool_jar=${BUNDLETOOL_JAR:-}
if [ -z "$bundle_aapt2" ] || [ ! -x "$bundle_aapt2" ]; then
    echo "BUNDLE_AAPT2 must name the executable AAPT2 from Google's Maven repository" >&2
    exit 2
fi
if [ -z "$bundletool_jar" ] || [ ! -f "$bundletool_jar" ]; then
    echo "BUNDLETOOL_JAR must name a bundletool all-in-one jar" >&2
    exit 2
fi

ndk_root=${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-$sdk_root/ndk/27.2.12479018}}
strip_tool="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
platform_jar="$sdk_root/platforms/android-36/android.jar"
source_apk="$project_dir/app/build/outputs/apk/debug/app-debug.apk"
output_dir="$project_dir/app/build/outputs/bundle/distribution"
output_aab="$output_dir/proximity.aab"

for required in "$strip_tool" "$platform_jar" "$source_apk"; do
    if [ ! -e "$required" ]; then
        echo "missing app-bundle input: $required" >&2
        exit 2
    fi
done
command -v java >/dev/null 2>&1 || {
    echo "java is required for bundletool" >&2
    exit 2
}
command -v jarsigner >/dev/null 2>&1 || {
    echo "jarsigner is required to sign the app bundle" >&2
    exit 2
}

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/proximity-bundle.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
compiled_resources="$work_dir/compiled-resources.zip"
proto_apk="$work_dir/proto.apk"
proto_dir="$work_dir/proto"
module_dir="$work_dir/base"
base_zip="$work_dir/base.zip"
unsigned_aab="$work_dir/proximity-unsigned.aab"
mkdir -p "$proto_dir" "$module_dir/manifest" "$module_dir/lib" "$output_dir"

"$bundle_aapt2" compile \
    --dir "$project_dir/app/src/main/res" \
    -o "$compiled_resources"

"$bundle_aapt2" link \
    --proto-format \
    -I "$platform_jar" \
    --manifest "$project_dir/app/src/main/AndroidManifest.xml" \
    --min-sdk-version 26 \
    --target-sdk-version 36 \
    --version-code 2 \
    --version-name 0.2.0 \
    -o "$proto_apk" \
    "$compiled_resources"

unzip -q "$proto_apk" -d "$proto_dir"
cp "$proto_dir/AndroidManifest.xml" "$module_dir/manifest/AndroidManifest.xml"
cp "$proto_dir/resources.pb" "$module_dir/resources.pb"
if [ -d "$proto_dir/res" ]; then
    cp -R "$proto_dir/res" "$module_dir/res"
fi

for abi in arm64-v8a armeabi-v7a x86_64; do
    target_dir="$module_dir/lib/$abi"
    mkdir -p "$target_dir"
    unzip -p "$source_apk" "lib/$abi/libproximity.so" >"$target_dir/libproximity.so"
    test -s "$target_dir/libproximity.so"
    "$strip_tool" --strip-unneeded "$target_dir/libproximity.so"
done

(
    cd "$module_dir"
    zip -q -r "$base_zip" .
)

java -jar "$bundletool_jar" build-bundle \
    --modules="$base_zip" \
    --output="$unsigned_aab"

keystore=${ANDROID_KEYSTORE:-}
keystore_password=${ANDROID_KEYSTORE_PASSWORD:-wegert-debug}
key_password=${ANDROID_KEY_PASSWORD:-$keystore_password}
key_alias=${ANDROID_KEY_ALIAS:-wegert-debug}
expected_signer_sha256=${ANDROID_EXPECTED_CERT_SHA256:-DE:9B:1D:47:C5:A6:5E:6D:46:A2:04:B7:9D:D9:EE:56:6B:9D:3C:98:32:BA:81:EB:C4:21:3D:33:92:E9:2F:F9}

if [ -z "$keystore" ] || [ ! -f "$keystore" ]; then
    echo "ANDROID_KEYSTORE is required for the test-signed app bundle" >&2
    exit 2
fi

signer_sha256=$(
    keytool -list -v \
        -keystore "$keystore" \
        -storepass "$keystore_password" \
        -alias "$key_alias" 2>/dev/null |
        sed -n 's/^[[:space:]]*SHA256: //p' |
        head -n 1
)
if [ "$signer_sha256" != "$expected_signer_sha256" ]; then
    echo "unexpected Android test signer: ${signer_sha256:-missing}" >&2
    exit 2
fi

jarsigner \
    -keystore "$keystore" \
    -storepass "$keystore_password" \
    -keypass "$key_password" \
    -sigalg SHA256withRSA \
    -digestalg SHA-256 \
    -signedjar "$output_aab" \
    "$unsigned_aab" \
    "$key_alias"

jarsigner -verify "$output_aab"
java -jar "$bundletool_jar" validate --bundle="$output_aab"

printf '%s\n' "$output_aab"
