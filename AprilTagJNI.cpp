#include "AprilTagJNI.h"
#include "apriltag.h"

#include "tag36h11.h"
#include "tag25h9.h"
#include "tag16h5.h"
#include "tagCircle21h7.h"
#include "tagCircle49h12.h"
#include "tagCustom48h12.h"
#include "tagStandard41h12.h"
#include "tagStandard52h13.h"
#include "apriltag_pose.h"

#include <vector>
#include <algorithm>
#include <math.h>

struct DetectorState
{
  int id;
  apriltag_detector_t *td;
  apriltag_family_t *tf;
  void (*tf_destroy)(apriltag_family_t *);
};

std::vector<DetectorState> detectors;

extern "C"
{

  JNIEXPORT jlong JNICALL Java_org_photonvision_vision_apriltag_AprilTagJNI_AprilTag_1Create(JNIEnv *env,
                                                                                             jclass cls, jstring jstr, jdouble decimate, jdouble blur, jint threads, jboolean debug, jboolean refine_edges, jint min_cluster_pixels,
                                                                                             jint maxErrorBits, jint extraDecisionMargin)
  {
    // Initialize tag detector with options
    apriltag_family_t *tf = NULL;
    // const char *famname = fam;
    const char *famname = env->GetStringUTFChars(jstr, 0);

    void (*tf_destroy_func)(apriltag_family_t *);

    if (!strcmp(famname, "tag36h11"))
    {
      tf = tag36h11_create();
      tf_destroy_func = tag36h11_destroy;
    }
    else if (!strcmp(famname, "tag25h9"))
    {
      tf = tag25h9_create();
      tf_destroy_func = tag25h9_destroy;
    }
    else if (!strcmp(famname, "tag16h5"))
    {
      tf = tag16h5_create();
      tf_destroy_func = tag16h5_destroy;
    }
    else if (!strcmp(famname, "tagCircle21h7"))
    {
      tf = tagCircle21h7_create();
      tf_destroy_func = tagCircle21h7_destroy;
    }
    else if (!strcmp(famname, "tagCircle49h12"))
    {
      tf = tagCircle49h12_create();
      tf_destroy_func = tagCircle49h12_destroy;
    }
    else if (!strcmp(famname, "tagStandard41h12"))
    {
      tf = tagStandard41h12_create();
      tf_destroy_func = tagStandard41h12_destroy;
    }
    else if (!strcmp(famname, "tagStandard52h13"))
    {
      tf = tagStandard52h13_create();
      tf_destroy_func = tagStandard52h13_destroy;
    }
    else if (!strcmp(famname, "tagCustom48h12"))
    {
      tf = tagCustom48h12_create();
      tf_destroy_func = tagCustom48h12_destroy;
    }
    else
    {
      printf("Unrecognized tag family name. Use e.g. \"tag36h11\".\n");
      env->ReleaseStringUTFChars(jstr, famname);
      return 0;
    }

    apriltag_detector_t *td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = (float)decimate;
    td->quad_sigma = (float)blur;
    td->nthreads = threads;
    td->debug = debug;
    td->refine_edges = refine_edges;
    td->qtp.min_cluster_pixels = (int) min_cluster_pixels;
    td->maxErrorBits = maxErrorBits;
    td->extraDecisionMargin = extraDecisionMargin;

    env->ReleaseStringUTFChars(jstr, famname);

    // printf("Looking for max\n");
    auto max = std::max_element(detectors.begin(), detectors.end(), [](DetectorState &a, DetectorState &b)
                                 { return a.id < b.id; }); // detectors.size();
    int index = 0;
    if(max != detectors.end()) index = max->id + 1;
    detectors.push_back({index, td, tf, tf_destroy_func});
    printf("Created detector at idx %i\n", index);
    return (jlong)index;
  }

#define WPI_JNI_MAKEJARRAY(T, F)                                     \
  inline T##Array MakeJ##F##Array(JNIEnv *env, T *data, size_t size) \
  {                                                                  \
    T##Array jarr = env->New##F##Array(size);                        \
    if (!jarr)                                                       \
    {                                                                \
      return nullptr;                                                \
    }                                                                \
    env->Set##F##ArrayRegion(jarr, 0, size, data);                   \
    return jarr;                                                     \
  }

  WPI_JNI_MAKEJARRAY(jboolean, Boolean)
  WPI_JNI_MAKEJARRAY(jbyte, Byte)
  WPI_JNI_MAKEJARRAY(jshort, Short)
  WPI_JNI_MAKEJARRAY(jlong, Long)
  WPI_JNI_MAKEJARRAY(jfloat, Float)
  WPI_JNI_MAKEJARRAY(jdouble, Double)

#undef WPI_JNI_MAKEJARRAY

  /**
   * Finds a class and keeps it as a global reference.
   *
   * Use with caution, as the destructor does NOT call DeleteGlobalRef due to
   * potential shutdown issues with doing so.
   */
  class JClass
  {
  public:
    JClass() = default;

    JClass(JNIEnv *env, const char *name)
    {
      jclass local = env->FindClass(name);
      if (!local)
      {
        return;
      }
      m_cls = static_cast<jclass>(env->NewGlobalRef(local));
      env->DeleteLocalRef(local);
    }

    void free(JNIEnv *env)
    {
      if (m_cls)
      {
        env->DeleteGlobalRef(m_cls);
      }
      m_cls = nullptr;
    }

    explicit operator bool() const { return m_cls; }

    operator jclass() const { return m_cls; }

  protected:
    jclass m_cls = nullptr;
  };

  JClass detectionClass;

  JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
  {
    JNIEnv *env;
    if (vm->GetEnv((void **)(&env), JNI_VERSION_1_6) != JNI_OK)
    {
      return JNI_ERR;
    }

    detectionClass = JClass(env, "org/photonvision/vision/apriltag/DetectionResult");

    if (!detectionClass)
    {
      printf("Couldn't find class!");
      return JNI_ERR;
    }

    return JNI_VERSION_1_6;
  }



  static jobject MakeJObject(JNIEnv *env, const apriltag_detection_t *detect,
    apriltag_pose_t& pose1, apriltag_pose_t& pose2,
    double error1, double error2)
  {
    // Constructor signature must match Java! I = int, F = float, [D = double array
    static jmethodID constructor =
        env->GetMethodID(detectionClass, "<init>", "(IIF[DDD[D[D[DD[D[DD)V");

    if (!constructor)
    {
      return nullptr;
    }

    if (!detect)
    {
      return nullptr;
    }

    // We have to copy the homography matrix and coners into jdoubles
    jdouble h[9]; // = new jdouble[9]{};
    for (int i = 0; i < 9; i++) {
      h[i] = detect->H->data[i];
    }

    jdouble corners[8]; // = new jdouble[8]{};
    for (int i = 0; i < 4; i++)
    {
      corners[i * 2] = detect->p[i][0];
      corners[i * 2 + 1] = detect->p[i][1];
    }

    jdoubleArray harr = MakeJDoubleArray(env, h, 9);
    jdoubleArray carr = MakeJDoubleArray(env, corners, 8);

    // The rotation of the target is encoded as a 3 by 3 rotation matrix, we'll convert to a row-major array
    jdouble pose1RotMat[9] = {0};
    jdouble pose2RotMat[9] = {0};

    for (int i = 0; i < 9; i++) {
      if (pose1.R) {
        pose1RotMat[i] = pose1.R->data[i];
      }
      if (pose2.R) {
        pose2RotMat[i] = pose2.R->data[i];
      }
    }

    // And translation a 3x1 vector (todo check axis order)
    jdouble pose1Trans[3] = {0};
    jdouble pose2Trans[3] = {0};
    for (int i = 0; i < 3; i++) {
      if (pose1.t && pose1.t->data) {
        pose1Trans[i] = pose1.t->data[i];
      }
      if (pose2.t && pose2.t->data) {
          pose2Trans[i] = pose2.t->data[i];
      }
    }

    jdoubleArray pose1rotArr = MakeJDoubleArray(env, pose1RotMat, 9);
    jdoubleArray pose2rotArr = MakeJDoubleArray(env, pose2RotMat, 9);
    jdoubleArray pose1transArr = MakeJDoubleArray(env, pose1Trans, 3);
    jdoubleArray pose2transArr = MakeJDoubleArray(env, pose2Trans, 3);
    jdouble err1 = error1;
    jdouble err2 = error2;

    // Actually call the constructor
    auto ret = env->NewObject(
        detectionClass, constructor,
        (jint)detect->id, (jint)detect->hamming, (jfloat)detect->decision_margin,
        harr, (jdouble)detect->c[0], (jdouble)detect->c[1], carr,
        pose1transArr, pose1rotArr, err1,
        pose2transArr, pose2rotArr, err2);

    // TODO we don't seem to need this... or at least, it doesnt leak rn
    // env->ReleaseDoubleArrayElements(harr, h, 0);
    // env->ReleaseDoubleArrayElements(carr, corners, 0);

    return ret;
  }

  JNIEXPORT jobjectArray JNICALL Java_org_photonvision_vision_apriltag_AprilTagJNI_AprilTag_1Detect(JNIEnv *env,
                                                                                                    jclass cls, jlong detectIdx, jlong pData,
                                                                                                    jint rows, jint cols,
    jboolean doPoseEstimation, jdouble tagWidthMeters, jdouble fx, jdouble fy, jdouble cx, jdouble cy, jint nIters)
  {
    // No image, can't do anything
    if (!pData) {
      return nullptr;
    }

    // Make an image_u8_t header for the Mat data
    image_u8_t im = {(int32_t)cols,
                     (int32_t)rows,
                     (int32_t)cols,
                     (uint8_t *)pData};

    // Get our detector
    auto state = std::find_if(detectors.begin(), detectors.end(), [&](DetectorState& s) { return s.id == detectIdx; });
    if (state == detectors.end()) {
      return nullptr;
    }

    // And run the detector on our new image
    zarray_t *detections = apriltag_detector_detect(state->td, &im);


    int size = zarray_size(detections);

    // Object array to return to Java
    jobjectArray jarr = env->NewObjectArray(size, detectionClass, nullptr);
    if (!jarr)
    {
      printf("Couldn't make array\n");
      return nullptr;
    }

    // Global pose
    apriltag_pose_t pose1 = { 0 };
    apriltag_pose_t pose2 = { 0 };

    // printf("Created array %llu! Got %i targets!\n", &jarr, size);
    //  Add our detected targets to the array
    for (size_t i = 0; i < size; ++i)
    {
      apriltag_detection_t *det = {0};
      zarray_get(detections, i, &det);

      if (det != nullptr)
      {
        double err1 = HUGE_VAL; //Should get overwritten if pose estimation is happening
        double err2 = HUGE_VAL;
        if (doPoseEstimation) {
          // Feed results to the pose estimator
          apriltag_detection_info_t info { det, tagWidthMeters, fx, fy, cx, cy };
          estimate_tag_pose_orthogonal_iteration(&info, &err1, &pose1, &err2, &pose2, nIters);
        }

        jobject obj = MakeJObject(env, det, pose1, pose2, err1, err2);

        env->SetObjectArrayElement(jarr, i, obj);
      }
    }

    // Now that stuff's in our Java-side array, we can clean up native memory
    apriltag_detections_destroy(detections);

    return jarr;
  }

  JNIEXPORT void JNICALL Java_org_photonvision_vision_apriltag_AprilTagJNI_AprilTag_1Destroy(JNIEnv *env, jclass clazz, jlong detectIdx)
  {
    printf("Destroying detector at idx %li\n", (long)detectIdx);


    auto state = std::find_if(detectors.begin(), detectors.end(), [&](DetectorState& s) { return s.id == detectIdx; });

    if(state == detectors.end()) return;

    if (state->td)
    {
      apriltag_detector_destroy(state->td);
      state->td = NULL;
    }
    if (state->tf)
    {
      state->tf_destroy(state->tf);
      state->tf = NULL;
    }

    detectors.erase(detectors.begin() + detectIdx);
  }
}