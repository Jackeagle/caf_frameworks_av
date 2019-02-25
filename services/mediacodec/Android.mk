LOCAL_PATH := $(call my-dir)

_software_codecs := \
    libstagefright_soft_aacdec \
    libstagefright_soft_aacenc \
    libstagefright_soft_amrdec \
    libstagefright_soft_amrnbenc \
    libstagefright_soft_amrwbenc \
    libstagefright_soft_avcdec \
    libstagefright_soft_avcenc \
    libstagefright_soft_flacdec \
    libstagefright_soft_flacenc \
    libstagefright_soft_g711dec \
    libstagefright_soft_gsmdec \
    libstagefright_soft_hevcdec \
    libstagefright_soft_mp3dec \
    libstagefright_soft_mpeg2dec \
    libstagefright_soft_mpeg4dec \
    libstagefright_soft_mpeg4enc \
    libstagefright_soft_opusdec \
    libstagefright_soft_rawdec \
    libstagefright_soft_vorbisdec \
    libstagefright_soft_vpxdec \
    libstagefright_soft_vpxenc \

# service executable
include $(CLEAR_VARS)
# seccomp is not required for coverage build.
ifneq ($(NATIVE_COVERAGE),true)
LOCAL_REQUIRED_MODULES_arm := crash_dump.policy mediacodec.policy
LOCAL_REQUIRED_MODULES_x86 := crash_dump.policy mediacodec.policy
endif
LOCAL_SRC_FILES := main_codecservice.cpp
LOCAL_SHARED_LIBRARIES := \
    libmedia_omx \
    libbinder \
    libutils \
    liblog \
    libbase \
    libavservices_minijail_vendor \
    libcutils \
    libhwbinder \
    libhidltransport \
    libstagefright_omx_ext \
    libstagefright_xmlparser \
    android.hardware.media.omx@1.0 \
    android.hidl.memory@1.0

LOCAL_MODULE := android.hardware.media.omx@1.0-service
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true
LOCAL_32_BIT_ONLY := true

ifeq ($(TARGET_ARCH),arm)
     LOCAL_CFLAGS += -DENABLE_BINDER_BUFFER_TUNING_FOR_32_BIT
endif

# Since this is 32-bit-only module, only 32-bit version of the codecs are installed.
# TODO(b/72343507): eliminate the need for manually adding .vendor suffix. This should be done
# by the build system.
LOCAL_REQUIRED_MODULES += \
$(foreach codec,$(_software_codecs),\
  $(eval _vendor_suffix := $(if $(BOARD_VNDK_VERSION),.vendor))\
  $(codec)$(_vendor_suffix)\
)
_software_codecs :=
LOCAL_INIT_RC := android.hardware.media.omx@1.0-service.rc

include $(BUILD_EXECUTABLE)

####################################################################

# service executable
include $(CLEAR_VARS)
# seccomp is not required for coverage build.
ifneq ($(NATIVE_COVERAGE),true)
LOCAL_REQUIRED_MODULES_arm := crash_dump.policy mediaswcodec.policy
LOCAL_REQUIRED_MODULES_arm64 := crash_dump.policy mediaswcodec.policy
LOCAL_REQUIRED_MODULES_x86 := crash_dump.policy mediaswcodec.policy
LOCAL_REQUIRED_MODULES_x86_64 := crash_dump.policy mediaswcodec.policy
endif

LOCAL_SRC_FILES := \
    main_swcodecservice.cpp \
    MediaCodecUpdateService.cpp \

sanitizer_runtime_libraries := $(call normalize-path-list,$(addsuffix .so,\
  $(ADDRESS_SANITIZER_RUNTIME_LIBRARY) \
  $(UBSAN_RUNTIME_LIBRARY) \
  $(TSAN_RUNTIME_LIBRARY) \
  $(2ND_ADDRESS_SANITIZER_RUNTIME_LIBRARY) \
  $(2ND_UBSAN_RUNTIME_LIBRARY) \
  $(2ND_TSAN_RUNTIME_LIBRARY)))

# $(info Sanitizer:  $(sanitizer_runtime_libraries))

llndk_libraries := $(call normalize-path-list,$(addsuffix .so,\
  $(LLNDK_LIBRARIES)))

# $(info LLNDK:  $(llndk_libraries))

LOCAL_CFLAGS := -DLINKED_LIBRARIES='"$(sanitizer_runtime_libraries):$(llndk_libraries)"'

LOCAL_SHARED_LIBRARIES := \
    libavservices_minijail \
    libbase \
    libbinder \
    libcutils \
    libhidltransport \
    libhwbinder \
    liblog \
    libmedia \
    libutils \
    libziparchive \

LOCAL_MODULE := mediaswcodec
LOCAL_INIT_RC := mediaswcodec.rc
LOCAL_SANITIZE := scudo
ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), x86_64 arm64))
  LOCAL_MULTILIB := both
  LOCAL_MODULE_STEM_32 := $(LOCAL_MODULE)32
  LOCAL_MODULE_STEM_64 := $(LOCAL_MODULE)
endif

sanitizer_runtime_libraries :=
llndk_libraries :=

include $(BUILD_EXECUTABLE)

####################################################################

# service seccomp policy
ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), x86 x86_64 arm arm64))
include $(CLEAR_VARS)
LOCAL_MODULE := mediacodec.policy
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/seccomp_policy
# mediacodec runs in 32-bit combatibility mode. For 64 bit architectures,
# use the 32 bit policy
ifdef TARGET_2ND_ARCH
  ifneq ($(TARGET_TRANSLATE_2ND_ARCH),true)
    LOCAL_SRC_FILES := seccomp_policy/mediacodec-$(TARGET_2ND_ARCH).policy
  else
    LOCAL_SRC_FILES := seccomp_policy/mediacodec-$(TARGET_ARCH).policy
  endif
else
    LOCAL_SRC_FILES := seccomp_policy/mediacodec-$(TARGET_ARCH).policy
endif
include $(BUILD_PREBUILT)
endif

####################################################################

# sw service seccomp policy
ifeq ($(TARGET_ARCH), $(filter $(TARGET_ARCH), x86 x86_64 arm arm64))
include $(CLEAR_VARS)
LOCAL_MODULE := mediaswcodec.policy
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/seccomp_policy
LOCAL_SRC_FILES := seccomp_policy/mediaswcodec-$(TARGET_ARCH).policy
include $(BUILD_PREBUILT)
endif

include $(call all-makefiles-under, $(LOCAL_PATH))
