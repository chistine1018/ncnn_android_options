project(yolov8ncnn)

cmake_minimum_required(VERSION 3.10)


## 如果要使用seg 要使用4.6.0
set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/opencv-mobile-4.6.0-android/sdk/native/jni)
#find_package(OpenCV REQUIRED)
find_package(OpenCV REQUIRED core imgproc)

set(ncnn_DIR ${CMAKE_SOURCE_DIR}/ncnn-20221128-android-vulkan/${ANDROID_ABI}/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

file(GLOB filter_files filter/*.cpp)
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/filter
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# SHARE 動態連結庫
add_library(yolov8ncnn SHARED yolov8ncnn.cpp yolo.cpp yolonormal.cpp blazepose.cpp landmark.cpp scrfd.cpp ndkcamera.cpp ${filter_files})

target_link_libraries(yolov8ncnn ncnn ${OpenCV_LIBS} camera2ndk mediandk)
