LOCAL_PATH := $(call my-dir)

# service library
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_SRC_FILES := \
    MediaExtractorService.cpp

LOCAL_SHARED_LIBRARIES := libmedia libstagefright libbinder libutils liblog
LOCAL_MODULE:= libmediaextractorservice

sanitizer_runtime_libraries := $(call normalize-path-list,$(addsuffix .so,\
  $(ADDRESS_SANITIZER_RUNTIME_LIBRARY) \
  $(UBSAN_RUNTIME_LIBRARY) \
  $(TSAN_RUNTIME_LIBRARY)))

# $(info Sanitizer:  $(sanitizer_runtime_libraries))

ndk_libraries := $(call normalize-path-list,$(addprefix lib,$(addsuffix .so,\
  $(NDK_PREBUILT_SHARED_LIBRARIES))))

# $(info NDK:  $(ndk_libraries))

LOCAL_CFLAGS += -DLINKED_LIBRARIES='"$(sanitizer_runtime_libraries):$(ndk_libraries)"'

sanitizer_runtime_libraries :=
ndk_libraries :=

include $(BUILD_SHARED_LIBRARY)


# service executable
include $(CLEAR_VARS)
# seccomp filters are defined for the following architectures:
LOCAL_REQUIRED_MODULES_arm := crash_dump.policy mediaextractor.policy
LOCAL_REQUIRED_MODULES_arm64 := crash_dump.policy mediaextractor.policy
LOCAL_REQUIRED_MODULES_x86 := crash_dump.policy mediaextractor.policy
LOCAL_REQUIRED_MODULES_x86_64 := crash_dump.policy mediaextractor.policy

LOCAL_SRC_FILES := main_extractorservice.cpp
LOCAL_SHARED_LIBRARIES := libmedia libmediaextractorservice libbinder libutils \
    liblog libbase libandroidicu libavservices_minijail
LOCAL_STATIC_LIBRARIES := libicuandroid_utils
LOCAL_MODULE:= mediaextractor
LOCAL_INIT_RC := mediaextractor.rc
LOCAL_C_INCLUDES := frameworks/av/media/libmedia
LOCAL_CFLAGS := -Wall -Werror
LOCAL_SANITIZE := scudo
include $(BUILD_EXECUTABLE)

# service seccomp filter
ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), arm arm64 x86 x86_64))
include $(CLEAR_VARS)
LOCAL_MODULE := mediaextractor.policy
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/seccomp_policy
LOCAL_SRC_FILES := seccomp_policy/mediaextractor-$(TARGET_ARCH).policy
include $(BUILD_PREBUILT)
endif
