Pod::Spec.new do |s|
  s.name             = 'smartmic_core'
  s.version          = '0.3.0'
  s.summary          = 'SmartMic native phone core'
  s.description      = <<-DESC
The pairing, codec, encryption and PTT engine, shared byte-for-byte with the
Windows service and the C++ test suite. Built ahead of time by
apps/phone-core/ios/build-ios-lib.sh and vendored here so that opening the
project in Xcode and pressing Run needs no extra tooling.
                       DESC
  s.homepage         = 'https://github.com/smartmic'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'SmartMic' => 'dev@smartmic.invalid' }
  s.source           = { :path => '.' }
  s.platform         = :ios, '13.0'
  # The pod vendors prebuilt .a files, so it can never be a dynamic framework.
  s.static_framework = true

  s.vendored_libraries = 'lib/libsmartmic_phone.a', 'lib/libsodium.a', 'lib/libopus.a'
  s.source_files       = 'include/**/*.h'
  s.public_header_files = 'include/**/*.h'

  # AudioToolbox is the microphone (AudioUnit / VoiceProcessingIO).
  s.frameworks = 'AudioToolbox', 'AVFoundation', 'CoreAudio'
  s.libraries  = 'c++'

  # Dart looks the symbols up at runtime through DynamicLibrary.process(), so
  # nothing references them at link time and the linker would otherwise drop
  # the whole archive. -force_load is what keeps them in the binary.
  s.user_target_xcconfig = {
    'OTHER_LDFLAGS' => '-force_load "${PODS_ROOT}/../smartmic_core/lib/libsmartmic_phone.a"'
  }
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386'
  }
end
