LOCAL_PATH := $(call my-dir)

# Add prebuilt Oboe library
include $(CLEAR_VARS)
LOCAL_MODULE := Oboe
LOCAL_SRC_FILES := $(OBOE_SDK_ROOT)/prefab/modules/oboe/libs/android.$(TARGET_ARCH_ABI)/liboboe.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE    := tutorial-6
LOCAL_SRC_FILES := tutorial-6.c dummy.cpp AudioPlayer.cpp
LOCAL_SHARED_LIBRARIES := gstreamer_android Oboe
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)

LOCAL_C_INCLUDES += $(OBOE_SDK_ROOT)/prefab/modules/oboe/include

ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif

LOCAL_CPPFLAGS := -std=c++11 -D __cplusplus=201103L -DXR_USE_PLATFORM_ANDROID -DXR_USE_GRAPHICS_API_OPENGL_ES -fexceptions
LOCAL_CXXFLAGS += -stdlib=libstdc++

ifeq ($(TARGET_ARCH_ABI),armeabi)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/armv7
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm64
else ifeq ($(TARGET_ARCH_ABI),x86)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86_64
else
$(error Target arch ABI not supported: $(TARGET_ARCH_ABI))
endif

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/
include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_SYS) opus opusparse
GSTREAMER_EXTRA_LIBS      := -liconv
GSTREAMER_EXTRA_DEPS      := gstreamer-audio-1.0 libsoup-2.4
include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
