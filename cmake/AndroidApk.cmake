# smx_android_apk(<target>): SuperMetroidX.apk in the build folder, from the
# shared library <target> (libmain.so), SDL's Java activity, and the icon
# drawn by smxicon. It is put together by the Android SDK's own tools, so the
# build needs neither Gradle nor Android Studio: javac, d8, aapt2, aapt,
# zipalign, and apksigner. The APK is signed with the SDK's debug key, which
# installs on an emulator or a phone in developer mode; it is not for a store.

# The oldest Android the game runs on is the NDK's ANDROID_PLATFORM.
set(SMX_ANDROID_MIN_SDK ${ANDROID_PLATFORM_LEVEL})
set(SMX_ANDROID_TARGET_SDK 35 CACHE STRING "Android API level the game is built for")

# The SDK is the NDK's grandparent folder ($SDK/ndk/<version>) unless named.
get_filename_component(smx_ndk_parent "${CMAKE_ANDROID_NDK}/../.." ABSOLUTE)
if(DEFINED ENV{ANDROID_HOME})
  set(smx_sdk_default "$ENV{ANDROID_HOME}")
else()
  set(smx_sdk_default "${smx_ndk_parent}")
endif()
set(SMX_ANDROID_SDK "${smx_sdk_default}" CACHE PATH "Android SDK with build-tools and platforms")
set(SMX_ANDROID_KEYSTORE "$ENV{HOME}/.android/debug.keystore"
    CACHE FILEPATH "Keystore that signs the APK; made with the SDK's debug key if it is missing"
)

# The newest version folder in `dir`, by version order.
function(smx_android_newest dir out)
  file(GLOB versions RELATIVE "${dir}" "${dir}/*")
  list(SORT versions COMPARE NATURAL ORDER DESCENDING)
  if(NOT versions)
    message(FATAL_ERROR "No Android SDK component in ${dir}; install it with Android Studio's SDK Manager.")
  endif()
  list(GET versions 0 newest)
  set(${out} "${dir}/${newest}" PARENT_SCOPE)
endfunction()

function(smx_android_apk target)
  set(platform "${SMX_ANDROID_SDK}/platforms/android-${SMX_ANDROID_TARGET_SDK}")
  set(android_jar "${platform}/android.jar")
  if(NOT EXISTS "${android_jar}")
    message(FATAL_ERROR "No ${android_jar}: install Android SDK Platform ${SMX_ANDROID_TARGET_SDK}, "
                        "or set SMX_ANDROID_SDK to the SDK folder."
    )
  endif()
  smx_android_newest("${SMX_ANDROID_SDK}/build-tools" tools)
  find_package(Java 11 REQUIRED COMPONENTS Runtime Development)
  # javac 21 and later name no constructor parameters of an anonymous class,
  # and the d8 of build-tools before 35 fails on that.
  get_filename_component(tools_version "${tools}" NAME)
  if(Java_VERSION VERSION_GREATER_EQUAL 21 AND tools_version VERSION_LESS 35)
    message(FATAL_ERROR "Java ${Java_VERSION} needs Android build-tools 35 or later; ${tools} is "
                        "older. Install newer build-tools, or set JAVA_HOME to a JDK 17."
    )
  endif()

  set(dir "${CMAKE_BINARY_DIR}/android")
  set(apk "${CMAKE_BINARY_DIR}/SuperMetroidX.apk")
  # 1.2.3 is 10203: each release's code is above the last's, as Android requires.
  math(EXPR SMX_ANDROID_VERSION_CODE
       "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}"
  )
  set(manifest "${dir}/AndroidManifest.xml")
  configure_file("${CMAKE_SOURCE_DIR}/packaging/android/AndroidManifest.xml.in" "${manifest}" @ONLY)

  # SDL's activity and the subclass that loads libmain.so, as one classes.dex.
  file(GLOB sdl_java "${sdl3_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app/*.java")
  set(java ${sdl_java} "${CMAKE_SOURCE_DIR}/packaging/android/SuperMetroidXActivity.java")
  set(classes_jar "${dir}/classes.jar")
  add_custom_command(
    OUTPUT "${classes_jar}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${dir}/classes"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}/classes"
    COMMAND "${Java_JAVAC_EXECUTABLE}" --release 11 -nowarn -Xlint:none -encoding UTF-8 -classpath
            "${android_jar}" -d "${dir}/classes" ${java}
    COMMAND "${Java_JAR_EXECUTABLE}" cf "${classes_jar}" -C "${dir}/classes" .
    DEPENDS ${java}
    COMMENT "Compiling SDL's Java activity"
    VERBATIM
  )
  set(dex "${dir}/stage/classes.dex")
  add_custom_command(
    OUTPUT "${dex}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}/stage"
    COMMAND "${tools}/d8" --release --min-api ${SMX_ANDROID_MIN_SDK} --lib "${android_jar}"
            --output "${dir}/stage" "${classes_jar}"
    DEPENDS "${classes_jar}"
    VERBATIM
  )

  # The launcher icon at each screen density, compiled for aapt2.
  set(flats "")
  foreach(icon "mdpi:48" "hdpi:72" "xhdpi:96" "xxhdpi:144" "xxxhdpi:192")
    string(REPLACE ":" ";" icon "${icon}")
    list(GET icon 0 density)
    list(GET icon 1 size)
    set(png "${dir}/res/mipmap-${density}/ic_launcher.png")
    set(flat "${dir}/flat/mipmap-${density}_ic_launcher.png.flat")
    add_custom_command(
      OUTPUT "${flat}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}/res/mipmap-${density}" "${dir}/flat"
      COMMAND smxicon png ${size} "${png}"
      COMMAND "${tools}/aapt2" compile -o "${dir}/flat" "${png}"
      DEPENDS smxicon
      VERBATIM
    )
    list(APPEND flats "${flat}")
  endforeach()

  # The library goes in for the one ABI of this build; Release strips it.
  set(lib "lib/${CMAKE_ANDROID_ARCH_ABI}/libmain.so")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(copy_lib "${CMAKE_STRIP}" --strip-unneeded -o "${dir}/stage/${lib}" "$<TARGET_FILE:${target}>")
  else()
    set(copy_lib ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target}>" "${dir}/stage/${lib}")
  endif()

  set(keystore "${SMX_ANDROID_KEYSTORE}")
  if(NOT EXISTS "${keystore}")
    set(keystore "${dir}/debug.keystore")
    get_filename_component(java_bin "${Java_JAVA_EXECUTABLE}" DIRECTORY)
    add_custom_command(
      OUTPUT "${keystore}"
      COMMAND "${java_bin}/keytool" -genkeypair -keystore "${keystore}" -storepass android -alias
              androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname
              "CN=Android Debug,O=Android,C=US"
      VERBATIM
    )
  endif()

  add_custom_command(
    OUTPUT "${apk}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}/stage/lib/${CMAKE_ANDROID_ARCH_ABI}"
    COMMAND ${copy_lib}
    COMMAND "${tools}/aapt2" link -o "${dir}/unaligned.apk" -I "${android_jar}" --manifest
            "${manifest}" --min-sdk-version ${SMX_ANDROID_MIN_SDK} --target-sdk-version
            ${SMX_ANDROID_TARGET_SDK} ${flats}
    COMMAND ${CMAKE_COMMAND} -E chdir "${dir}/stage" "${tools}/aapt" add -v "${dir}/unaligned.apk"
            classes.dex "${lib}"
    COMMAND "${tools}/zipalign" -f -p 4 "${dir}/unaligned.apk" "${dir}/aligned.apk"
    COMMAND "${tools}/apksigner" sign --ks "${keystore}" --ks-pass pass:android --ks-key-alias
            androiddebugkey --key-pass pass:android --out "${apk}" "${dir}/aligned.apk"
    DEPENDS ${target} "${dex}" ${flats} "${manifest}" "${keystore}"
    COMMENT "Packing ${apk}"
    VERBATIM
  )
  add_custom_target(smx_apk ALL DEPENDS "${apk}")
endfunction()
