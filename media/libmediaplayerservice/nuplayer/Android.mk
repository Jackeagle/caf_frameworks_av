LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#            Common definitons
# ---------------------------------------------------------------------------------

libnuplayer-wfd-def := -DQCOM_WFD_SINK

LOCAL_SRC_FILES:=                       \
        GenericSource.cpp               \
        HTTPLiveSource.cpp              \
        NuPlayer.cpp                    \
        NuPlayerDecoder.cpp             \
        NuPlayerDriver.cpp              \
        NuPlayerRenderer.cpp            \
        NuPlayerStreamListener.cpp      \
        RTSPSource.cpp                  \
        StreamingSource.cpp             \
        NuPlayerStats.cpp               \
        MPQHALWrapper.cpp               \
        WFDRenderer.cpp

LOCAL_CFLAGS := $(libnuplayer-wfd-def)
LOCAL_C_INCLUDES := \
	$(TOP)/frameworks/av/media/libstagefright/httplive            \
	$(TOP)/frameworks/av/media/libstagefright/include             \
	$(TOP)/frameworks/av/media/libstagefright/mpeg2ts             \
	$(TOP)/frameworks/av/media/libstagefright/rtsp                \
	$(TOP)/frameworks/native/include/media/openmax                \
        $(TOP)/frameworks/av/media/libstagefright/timedtext           \
	$(TOP)/frameworks/av/media/libstagefright/utils               \
	$(TOP)/hardware/qcom/display/libgralloc

ifeq ($(call is-chipset-in-board-platform,msm8960),true)
   LOCAL_CFLAGS += -DUSE_HWCPLL_CORRECTION
endif

LOCAL_MODULE:= libstagefright_nuplayer

LOCAL_MODULE_TAGS := eng

include $(BUILD_STATIC_LIBRARY)

