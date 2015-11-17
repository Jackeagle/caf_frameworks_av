LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    AudioPolicyService.cpp \
    AudioPolicyEffects.cpp

ifeq ($(USE_LEGACY_AUDIO_POLICY), 1)
LOCAL_SRC_FILES += \
    AudioPolicyInterfaceImplLegacy.cpp \
    AudioPolicyClientImplLegacy.cpp

    LOCAL_CFLAGS += -DUSE_LEGACY_AUDIO_POLICY
else
LOCAL_SRC_FILES += \
    AudioPolicyInterfaceImpl.cpp \
    AudioPolicyClientImpl.cpp
endif

LOCAL_C_INCLUDES := \
    $(TOPDIR)frameworks/av/services/audioflinger \
    $(call include-path-for, audio-effects) \
    $(call include-path-for, audio-utils)

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    liblog \
    libbinder \
    libmedia \
    libhardware \
    libhardware_legacy \
    libserviceutility

ifneq ($(USE_LEGACY_AUDIO_POLICY), 1)
LOCAL_SHARED_LIBRARIES += \
    libaudiopolicymanager
endif

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper

LOCAL_MODULE:= libaudiopolicyservice

LOCAL_CFLAGS += -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)


ifneq ($(USE_LEGACY_AUDIO_POLICY), 1)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    AudioPolicyManager.cpp

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)),true)
  AUDIO_FEATURE_FLAGS := -DDTS_EAGLE
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
  LOCAL_SRC_FILES += AudioUtil.c
endif

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    liblog \
    libsoundtrigger

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_VOIP)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_COMPRESS_VOIP_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTN_FORMATS)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_FORMATS_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FM)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_FM_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_SPK)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_HDMI_SPK_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INCALL_MUSIC)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_INCALL_MUSIC_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_MULTIPLE_TUNNEL)), true)
AUDIO_FEATURE_FLAGS += -DMULTIPLE_OFFLOAD_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PCM_OFFLOAD)),true)
    AUDIO_FEATURE_FLAGS += -DPCM_OFFLOAD_ENABLED_16
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24)),true)
    AUDIO_FEATURE_FLAGS += -DPCM_OFFLOAD_ENABLED_24
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_AFE_PROXY_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SSR)),true)
AUDIO_FEATURE_FLAGS += -DAUDIO_EXTN_SSR_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_VOICE_CONCURRENCY)),true)
AUDIO_FEATURE_FLAGS += -DVOICE_CONCURRENCY
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_RECORD_PLAY_CONCURRENCY)),true)
AUDIO_FEATURE_FLAGS += -DRECORD_PLAY_CONCURRENCY
endif

ifeq ($(strip $(DOLBY_UDC)),true)
  AUDIO_FEATURE_FLAGS += -DDOLBY_UDC
endif #DOLBY_UDC
ifeq ($(strip $(DOLBY_DDP)),true)
  AUDIO_FEATURE_FLAGS += -DDOLBY_DDP
endif #DOLBY_DDP
ifeq ($(strip $(DOLBY_DAP)),true)
    ifdef DOLBY_DAP_OPENSLES
        AUDIO_FEATURE_FLAGS += -DDOLBY_DAP_OPENSLES
    endif
endif #DOLBY_END

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH)),true)
    AUDIO_FEATURE_FLAGS += -DHDMI_PASSTHROUGH_ENABLED
endif

LOCAL_CFLAGS += $(AUDIO_FEATURE_FLAGS)

LOCAL_MODULE:= libaudiopolicymanagerdefault

include $(BUILD_SHARED_LIBRARY)

ifneq ($(USE_CUSTOM_AUDIO_POLICY), 1)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    AudioPolicyFactory.cpp

LOCAL_SHARED_LIBRARIES := \
    libaudiopolicymanagerdefault

LOCAL_CFLAGS += $(AUDIO_FEATURE_FLAGS)

LOCAL_MODULE:= libaudiopolicymanager

include $(BUILD_SHARED_LIBRARY)

endif
endif
