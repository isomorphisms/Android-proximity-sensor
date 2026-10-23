#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$sdk_root" ]; then
    echo "ANDROID_HOME or ANDROID_SDK_ROOT is required" >&2
    exit 2
fi

ndk_root=${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-$sdk_root/ndk/27.2.12479018}}
toolchain="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64"
glue_dir="$ndk_root/sources/android/native_app_glue"
build_tools="$sdk_root/build-tools/36.0.0"
platform_jar="$sdk_root/platforms/android-36/android.jar"
strip_tool="$toolchain/bin/llvm-strip"

for required in \
    "$toolchain/bin/aarch64-linux-android26-clang" \
    "$toolchain/bin/armv7a-linux-androideabi26-clang" \
    "$toolchain/bin/x86_64-linux-android26-clang" \
    "$strip_tool" \
    "$glue_dir/android_native_app_glue.c" \
    "$build_tools/aapt2" \
    "$build_tools/zipalign" \
    "$build_tools/apksigner" \
    "$platform_jar"
do
    if [ ! -e "$required" ]; then
        echo "missing Android build dependency: $required" >&2
        exit 2
    fi
done

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/proximity-build.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
staging_dir="$work_dir/staging"
debug_output_dir="$project_dir/app/build/outputs/apk/debug"
distribution_output_dir="$project_dir/app/build/outputs/apk/distribution"
mkdir -p "$staging_dir" "$debug_output_dir" "$distribution_output_dir"

compile_abi() {
    abi=$1
    compiler=$2
    arm_flags=$3
    abi_objects="$work_dir/objects/$abi"
    mkdir -p "$abi_objects" "$staging_dir/lib/$abi"

    common="-std=c17 -O2 -g -fPIC -fstack-protector-strong -Wall -Wextra -Werror -Wpedantic -Wshadow"
    "$toolchain/bin/$compiler" $arm_flags $common \
        -I"$project_dir/android" -I"$project_dir/model" -I"$glue_dir" \
        -c "$project_dir/app/src/main/c/native_main.c" -o "$abi_objects/native_main.o"
    "$toolchain/bin/$compiler" $arm_flags $common \
        -I"$project_dir/android" \
        -c "$project_dir/android/android_proximity.c" -o "$abi_objects/android_proximity.o"
    "$toolchain/bin/$compiler" $arm_flags -std=c17 -O2 -g -fPIC \
        -I"$glue_dir" -c "$glue_dir/android_native_app_glue.c" \
        -o "$abi_objects/android_native_app_glue.o"

    "$toolchain/bin/$compiler" $arm_flags -shared \
        -Wl,--no-undefined -Wl,--gc-sections -Wl,-z,relro,-z,now \
        -Wl,-u,ANativeActivity_onCreate \
        "$abi_objects/native_main.o" "$abi_objects/android_proximity.o" \
        "$abi_objects/android_native_app_glue.o" -landroid -llog -lm \
        -o "$staging_dir/lib/$abi/libproximity.so"

    "$toolchain/bin/$compiler" $arm_flags -std=c17 -O2 -fPIE -pie \
        -Wall -Wextra -Werror -Wpedantic -Wshadow \
        -I"$project_dir/android" -I"$project_dir/model" \
        "$project_dir/command/proximity.c" "$project_dir/android/android_proximity.c" \
        -landroid -lm -o "$distribution_output_dir/proximity-$abi"
}

compile_abi arm64-v8a aarch64-linux-android26-clang ""
compile_abi armeabi-v7a armv7a-linux-androideabi26-clang "-mthumb -march=armv7-a"
compile_abi x86_64 x86_64-linux-android26-clang ""

base_apk="$work_dir/base.apk"
compiled_resources="$work_dir/compiled-resources.zip"

"$build_tools/aapt2" compile \
    --dir "$project_dir/app/src/main/res" \
    -o "$compiled_resources"

"$build_tools/aapt2" link \
    -I "$platform_jar" \
    --manifest "$project_dir/app/src/main/AndroidManifest.xml" \
    --min-sdk-version 26 \
    --target-sdk-version 36 \
    --version-code 1 \
    --version-name 0.1.0 \
    -o "$base_apk" \
    "$compiled_resources"

keystore=${ANDROID_KEYSTORE:-}
keystore_password=${ANDROID_KEYSTORE_PASSWORD:-wegert-debug}
key_password=${ANDROID_KEY_PASSWORD:-$keystore_password}
key_alias=${ANDROID_KEY_ALIAS:-wegert-debug}
expected_signer_sha256=${ANDROID_EXPECTED_CERT_SHA256:-DE:9B:1D:47:C5:A6:5E:6D:46:A2:04:B7:9D:D9:EE:56:6B:9D:3C:98:32:BA:81:EB:C4:21:3D:33:92:E9:2F:F9}

if [ -z "$keystore" ]; then
    echo "ANDROID_KEYSTORE is required; refusing to generate a throwaway APK signer" >&2
    exit 2
fi
if [ ! -f "$keystore" ]; then
    echo "missing Android signing keystore: $keystore" >&2
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

sign_apk() {
    input_apk=$1
    output_apk=$2
    label=$3

    "$build_tools/apksigner" sign \
        --ks "$keystore" \
        --ks-key-alias "$key_alias" \
        --ks-pass "pass:$keystore_password" \
        --key-pass "pass:$key_password" \
        --out "$output_apk" \
        "$input_apk"

    "$build_tools/apksigner" verify --verbose --print-certs "$output_apk" |
        tee "$work_dir/apk-signing-$label.txt"
    grep -Fq "Signer #1 certificate SHA-256 digest: $(printf '%s' "$expected_signer_sha256" | tr '[:upper:]' '[:lower:]' | tr -d ':')" "$work_dir/apk-signing-$label.txt"
}

package_apk() {
    label=$1
    output_apk=$2
    strip_native=$3
    shift 3

    package_dir="$work_dir/package-$label"
    package_staging="$package_dir/staging"
    unsigned_apk="$package_dir/unsigned.apk"
    aligned_apk="$package_dir/aligned.apk"
    mkdir -p "$package_staging"

    for abi in "$@"; do
        target_dir="$package_staging/lib/$abi"
        mkdir -p "$target_dir"
        cp "$staging_dir/lib/$abi/libproximity.so" "$target_dir/libproximity.so"
        if [ "$strip_native" = yes ]; then
            "$strip_tool" --strip-unneeded "$target_dir/libproximity.so"
        fi
    done

    cp "$base_apk" "$unsigned_apk"
    (
        cd "$package_staging"
        zip -0 -q -r "$unsigned_apk" lib
    )

    "$build_tools/zipalign" -f -P 16 4 "$unsigned_apk" "$aligned_apk"
    sign_apk "$aligned_apk" "$output_apk" "$label"
}

# Preserve the existing universal debug artifact for exact-head testing.
package_apk \
    universal-debug \
    "$debug_output_dir/app-debug.apk" \
    no \
    arm64-v8a armeabi-v7a x86_64

# Direct-distribution APKs contain stripped native code. A person downloading
# directly can take only the ABI their device executes; the universal file is
# retained as a fallback when the ABI is not known.
package_apk \
    armeabi-v7a \
    "$distribution_output_dir/proximity-armeabi-v7a.apk" \
    yes \
    armeabi-v7a
package_apk \
    arm64-v8a \
    "$distribution_output_dir/proximity-arm64-v8a.apk" \
    yes \
    arm64-v8a
package_apk \
    x86_64 \
    "$distribution_output_dir/proximity-x86_64.apk" \
    yes \
    x86_64
package_apk \
    universal \
    "$distribution_output_dir/proximity-universal.apk" \
    yes \
    arm64-v8a armeabi-v7a x86_64

printf '%s\n' "$debug_output_dir/app-debug.apk"
printf '%s\n' "$distribution_output_dir/proximity-armeabi-v7a.apk"
printf '%s\n' "$distribution_output_dir/proximity-arm64-v8a.apk"
printf '%s\n' "$distribution_output_dir/proximity-x86_64.apk"
printf '%s\n' "$distribution_output_dir/proximity-universal.apk"
printf '%s\n' "$distribution_output_dir/proximity-armeabi-v7a"
