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

extern "C"
{

  JNIEXPORT jlong JNICALL Java_org_photonvision_vision_apriltag_AprilTagJNI_AprilTag_1Create(JNIEnv *env,
                                                                                             jclass cls, jstring jstr, jdouble decimate, jdouble blur, jint threads, jboolean debug, jboolean refine_edges)
  {
    // Initialize tag detector with options
    apriltag_family_t *tf = NULL;
    // const char *famname = fam;
    const char *famname = env->GetStringUTFChars(jstr, 0);
    if (!strcmp(famname, "tag36h11"))
    {
      tf = tag36h11_create();
    }
    else if (!strcmp(famname, "tag25h9"))
    {
      tf = tag25h9_create();
    }
    else if (!strcmp(famname, "tag16h5"))
    {
      tf = tag16h5_create();
    }
    else if (!strcmp(famname, "tagCircle21h7"))
    {
      tf = tagCircle21h7_create();
    }
    else if (!strcmp(famname, "tagCircle49h12"))
    {
      tf = tagCircle49h12_create();
    }
    else if (!strcmp(famname, "tagStandard41h12"))
    {
      tf = tagStandard41h12_create();
    }
    else if (!strcmp(famname, "tagStandard52h13"))
    {
      tf = tagStandard52h13_create();
    }
    else if (!strcmp(famname, "tagCustom48h12"))
    {
      tf = tagCustom48h12_create();
    }
    else
    {
      printf("Unrecognized tag family name. Use e.g. \"tag36h11\".\n");
      env->ReleaseStringUTFChars(jstr, famname);
      return 0;
    }

    apriltag_detector_t *td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = decimate;
    td->quad_sigma = blur;
    td->nthreads = threads;
    td->debug = debug;
    td->refine_edges = refine_edges;

    env->ReleaseStringUTFChars(jstr, famname);

    printf("Created detector at %llu\n", td);
    return (jlong)td;
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

  static jobject MakeJObject(JNIEnv *env, const apriltag_detection_t *detect)
  {
    static jmethodID constructor =
        env->GetMethodID(detectionClass, "<init>", "(IIF[DDD[D)V");

    if (!constructor)
    {
      return nullptr;
    }

    if (!detect)
    {
      return nullptr;
    }

    // We have to copy the homography matrix and coners into jdoubles
    jdouble *h = new jdouble[9]{};
    for (int i = 0; i < 9; i++)
      h[i] = detect->H->data[i];
    jdouble *corners = new jdouble[8]{};
    for (int i = 0; i < 4; i++)
    {
      corners[i * 2] = detect->p[i][0];
      corners[i * 2 + 1] = detect->p[i][1];
    }

    jdoubleArray harr = MakeJDoubleArray(env, h, 9);
    jdoubleArray carr = MakeJDoubleArray(env, corners, 8);

    // Actually call the constructor
    auto ret = env->NewObject(
        detectionClass, constructor,
        (jint)detect->id, (jint)detect->hamming, (jfloat)detect->decision_margin,
        harr, (jdouble)detect->c[0], (jdouble)detect->c[1], carr);


    // // I think this prevents us from leaking new double arrays every time
    // env->ReleaseDoubleArrayElements(harr, h, 0);
    // env->ReleaseDoubleArrayElements(carr, corners, 0);

    // return nullptr;

    return ret;
  }

  JNIEXPORT jobjectArray JNICALL Java_org_photonvision_vision_apriltag_AprilTagJNI_AprilTag_1Detect(JNIEnv *env,
                                                                                                    jclass cls, jlong detector, jlong pData,
                                                                                                    jint rows, jint cols)
  {
    // Make an image_u8_t header for the Mat data
    image_u8_t im = {(int32_t)cols,
                     (int32_t)rows,
                     (int32_t)cols,
                     (uint8_t *)pData};

    // Get our detector
    apriltag_detector_t *td = (apriltag_detector_t *)detector;

    //printf("Running detector, detector = %llu td = %llu env = %llu\n", detector, (long long unsigned) td, env);

    // And run the detector on our new image
    zarray_t *detections = apriltag_detector_detect(td, &im);
    //printf("Ran\n");
    int size = zarray_size(detections);

    // Object array to return to Java
    jobjectArray jarr = env->NewObjectArray(size, detectionClass, nullptr);
    if (!jarr)
    {
      printf("Couldn't make array\n");
      return nullptr;
    }

    //printf("Created array %llu! Got %i targets!\n", &jarr, size);
    // Add our detected targets to the array
    for (size_t i = 0; i < size; ++i)
    {
      apriltag_detection_t *det;
      zarray_get(detections, i, &det);
      //printf("Got %i\n", det);

      if(det != nullptr) {
        jobject obj = MakeJObject(env, det);
        env->SetObjectArrayElement(jarr, i, obj);
        // printf("Set element of array %i and idx %i to %i\n", &jarr, i, obj);
      }
    }

    //printf("Returning %i\n", jarr);
    return jarr;
  }
}