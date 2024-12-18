// Tencent is pleased to support the open source community by making ncnn available.
//
// Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>

#include <android/log.h>

#include <jni.h>

#include <string>
#include <vector>

#include <platform.h>
#include <benchmark.h>

#include "yolo.h"
#include "scrfd.h"

#include "ndkcamera.h"
#include "yolonormal.h"
#include "blazepose.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#if __ARM_NEON

#include <arm_neon.h>

#endif // __ARM_NEON

static int draw_unsupported(cv::Mat &rgb) {
    const char text[] = "unsupported";

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 1.0, 1, &baseLine);

    int y = (rgb.rows - label_size.height) / 2;
    int x = (rgb.cols - label_size.width) / 2;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y),
                                cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0));

    return 0;
}

static int draw_fps(cv::Mat &rgb) {
    // resolve moving average
    float avg_fps = 0.f;
    {
        static double t0 = 0.f;
        static float fps_history[10] = {0.f};

        double t1 = ncnn::get_current_time();
        if (t0 == 0.f) {
            t0 = t1;
            return 0;
        }

        float fps = 1000.f / (t1 - t0);
        t0 = t1;

        for (int i = 9; i >= 1; i--) {
            fps_history[i] = fps_history[i - 1];
        }
        fps_history[0] = fps;

        if (fps_history[9] == 0.f) {
            return 0;
        }

        for (int i = 0; i < 10; i++) {
            avg_fps += fps_history[i];
        }
        avg_fps /= 10.f;
    }

    char text[32];
    sprintf(text, "FPS=%.2f", avg_fps);

    int baseLine = 0;
    cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);

    int y = 0;
    int x = rgb.cols - label_size.width;

    cv::rectangle(rgb, cv::Rect(cv::Point(x, y),
                                cv::Size(label_size.width, label_size.height + baseLine)),
                  cv::Scalar(255, 255, 255), -1);

    cv::putText(rgb, text, cv::Point(x, y + label_size.height),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));

    return 0;
}

static SCRFD *g_scrfd = 0;
static Yolo *g_yolo = 0;
static YoloNormal *g_yolonormal = 0;
static BlazePose *g_blazepose = 0;

static int modelID = 0;

static ncnn::Mutex lock;

class MyNdkCamera : public NdkCameraWindow {
public:
    virtual void on_image_render(cv::Mat &rgb) const;
};

void MyNdkCamera::on_image_render(cv::Mat &rgb) const {
    // nanodet
    {
        ncnn::MutexLockGuard g(lock);

        // 如果模型是 face
        if (modelID == 2 || modelID == 3) {
            if (g_scrfd) {
                std::vector<FaceObject> faceobjects;
                g_scrfd->detect(rgb, faceobjects);

                g_scrfd->draw(rgb, faceobjects);
            } else {
                draw_unsupported(rgb);
            }

        } else if (modelID == 4 || modelID == 5) {
            if (g_yolonormal) {
                std::vector<ObjectNormal> objects;
                g_yolonormal->detect(rgb, objects);

                g_yolonormal->draw(rgb, objects);
            } else {
                draw_unsupported(rgb);
            }
        } else if (modelID == 6 || modelID == 7 || modelID == 8) {
            if (g_blazepose) {
                std::vector<Object> faceobjects;
                g_blazepose->detect(rgb, faceobjects);

                g_blazepose->draw(rgb, faceobjects);
            } else {
                draw_unsupported(rgb);
            }
        } else {
            // 如果模型是 seg
            if (g_yolo) {
                std::vector<ObjectSeg> objects;
                g_yolo->detect(rgb, objects);

                g_yolo->draw(rgb, objects);
            } else {
                draw_unsupported(rgb);
            }
        }
    }

    draw_fps(rgb);
}

static MyNdkCamera *g_camera = 0;

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "JNI_OnLoad");

    g_camera = new MyNdkCamera;

    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "JNI_OnUnload");

    {
        ncnn::MutexLockGuard g(lock);


        delete g_scrfd;
        g_scrfd = 0;

        delete g_yolo;
        g_yolo = 0;

        delete g_yolonormal;
        g_yolonormal = 0;

        delete g_blazepose;
        g_blazepose = 0;


    }

    delete g_camera;
    g_camera = 0;
}

// public native boolean loadModel(AssetManager mgr, int modelid, int cpugpu);
JNIEXPORT jboolean JNICALL
Java_com_asn_yolov8_options_Yolov8Ncnn_loadModel(JNIEnv *env, jobject thiz, jobject assetManager,
                                                 jint modelid, jint cpugpu) {
    if (modelid < 0 || modelid > 8 || cpugpu < 0 || cpugpu > 1) {
        return JNI_FALSE;
    }

    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "loadModel %p", mgr);

    // 設置模型type
    const char *modeltypes[] =
            {
                    "n",
                    "s",
                    "500m",
                    "1g",
                    "n",
                    "s",
                    "lite",
                    "full",
                    "heavy",
            };

    const int target_sizes[] =
            {
                    320,
                    320,
            };

    const float mean_vals[][3] =
            {
                    {103.53f, 116.28f, 123.675f},
                    {103.53f, 116.28f, 123.675f},
            };

    const float norm_vals[][3] =
            {
                    {1 / 255.f, 1 / 255.f, 1 / 255.f},
                    {1 / 255.f, 1 / 255.f, 1 / 255.f},
            };

    const char *modeltype = modeltypes[(int) modelid];
    modelID = modelid;

    int target_size = target_sizes[(int) modelid];
    if ((int) modelid == 2 or (int) modelid == 3) {
        target_size = target_sizes[(int) modelid - 2];
    }
    target_size = target_sizes[(int) modelid];
    bool use_gpu = (int) cpugpu == 1;
    target_size = 320;

    // reload
    {
        ncnn::MutexLockGuard g(lock);

        if (use_gpu && ncnn::get_gpu_count() == 0) {
            // no gpu
            delete g_scrfd;
            g_scrfd = 0;
            delete g_yolo;
            g_yolo = 0;
            delete g_yolonormal;
            g_yolonormal = 0;
            delete g_blazepose;
            g_blazepose = 0;
        } else {
            if (modelid == 2 || modelid == 3) {
                if (!g_scrfd)
                    g_scrfd = new SCRFD;
                g_scrfd->load(mgr, modeltype, use_gpu);
                __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "face");

            } else if (modelid == 4 || modelid == 5) {
                if (!g_yolonormal)
                    g_yolonormal = new YoloNormal;
                g_yolonormal->load(mgr, modeltype, target_size, mean_vals[(int) 0],
                                   norm_vals[(int) 0], use_gpu);
                __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "normal");

            } else if (modelid == 6 || modelid == 7 || modelid ==8) {
                if (!g_blazepose)
                    g_blazepose = new BlazePose;
                g_blazepose->load(mgr, modeltype, use_gpu);
                __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "yolopose");

            } else {

                if (!g_yolo)
                    g_yolo = new Yolo;
                g_yolo->load(mgr, modeltype, target_size, mean_vals[(int) 0],
                             norm_vals[(int) 0], use_gpu);
                __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "seg");

            }
        }
    }

    return JNI_TRUE;
}

// public native boolean openCamera(int facing);
JNIEXPORT jboolean JNICALL
Java_com_asn_yolov8_options_Yolov8Ncnn_openCamera(JNIEnv *env, jobject thiz, jint facing) {
    if (facing < 0 || facing > 1)
        return JNI_FALSE;

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "openCamera %d", facing);

    g_camera->open((int) facing);

    return JNI_TRUE;
}

// public native boolean closeCamera();
JNIEXPORT jboolean JNICALL
Java_com_asn_yolov8_options_Yolov8Ncnn_closeCamera(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "closeCamera");

    g_camera->close();

    return JNI_TRUE;
}

// public native boolean setOutputWindow(Surface surface);
JNIEXPORT jboolean JNICALL
Java_com_asn_yolov8_options_Yolov8Ncnn_setOutputWindow(JNIEnv *env, jobject thiz, jobject surface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);

    __android_log_print(ANDROID_LOG_DEBUG, "ncnn", "setOutputWindow %p", win);

    g_camera->set_window(win);

    return JNI_TRUE;
}

}
