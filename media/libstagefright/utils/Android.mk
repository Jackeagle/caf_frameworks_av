LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#            Common definitons
# ---------------------------------------------------------------------------------


LOCAL_SRC_FILES:=                       \
	InFpsEstimator.cpp

LOCAL_C_INCLUDES := \
	$(TOP)/frameworks/av/media/libstagefright/httplive            \
	$(TOP)/frameworks/av/media/libstagefright/include             \
	$(TOP)/frameworks/av/media/libstagefright/mpeg2ts             \
	$(TOP)/frameworks/av/media/libstagefright/rtsp                \
	$(TOP)/frameworks/native/include/media/openmax                \
        $(TOP)/frameworks/av/media/libstagefright/timedtext           \
	$(TOP)/frameworks/av/media/libstagefright/utils 	      \
	$(TOP)/hardware/qcom/display/libgralloc			      \
	$(TOP)/hardware/qcom/display/libhwcomposer


LOCAL_SHARED_LIBRARIES := \
        libbinder \
        libcutils \
        libdl \


LOCAL_MODULE:= libIfe

LOCAL_MODULE_TAGS := eng

include $(BUILD_SHARED_LIBRARY)

