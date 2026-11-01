LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp .cc
LOCAL_MODULE := PersonalRadio
LOCAL_SRC_FILES := main.cpp mod/logger.cpp mod/config.cpp
LOCAL_C_INCLUDES += $(LOCAL_PATH)/aml-psdk-gtasa $(LOCAL_PATH)/aml-psdk-gtasa/aml-psdk/game_sa
LOCAL_CFLAGS += -O2 -mfloat-abi=softfp -DNDEBUG -std=c++17
LOCAL_CXXFLAGS += -std=c++17 -fexceptions
LOCAL_LDLIBS += -llog -ldl
include $(BUILD_SHARED_LIBRARY)
