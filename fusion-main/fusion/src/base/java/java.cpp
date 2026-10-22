#include "java.h"
#include <stdexcept>

JavaVM* vm;
jobject classLoader;
jmethodID mid_findClass;

void setupClassLoader()
{
    // Reuse Fusion's JVMTI discovery to find the loader of the already loaded
    // Minecraft class. Avoid calling protected findClass on unrelated threads.
    jclass minecraft = Java::findClass(Java::Env, Java::tiEnv, "net/minecraft/client/Minecraft");
    if (!minecraft) return;
    jclass classClass = Java::Env->FindClass("java/lang/Class");
    jclass loaderClass = Java::Env->FindClass("java/lang/ClassLoader");
    const auto getLoader = Java::Env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    mid_findClass = Java::Env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (getLoader && mid_findClass) classLoader = Java::Env->CallObjectMethod(minecraft, getLoader);
    Java::Env->DeleteLocalRef(minecraft);
    Java::Env->DeleteLocalRef(classClass);
    Java::Env->DeleteLocalRef(loaderClass);
    if (Java::Env->ExceptionCheck()) { Java::Env->ExceptionClear(); return; }
    Java::Initialized = classLoader != nullptr;
}

void Java::Init()
{
    Java::Initialized = false;

    // Check if there is any Java VMs in the injected thread
    jsize count;
    if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0)
        return;


    jint res = vm->GetEnv((void**)&Java::Env, JNI_VERSION_1_6);

    if (res == JNI_EDETACHED)
        res = vm->AttachCurrentThread((void**)&Java::Env, nullptr);

    if (res != JNI_OK)
        return;

    if (!Java::Env || vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION) != JNI_OK || !Java::tiEnv) return;
    setupClassLoader();
}

void Java::Kill()
{
    if (Java::Env && classLoader) Java::Env->DeleteLocalRef(classLoader);
    classLoader = nullptr;
    Java::Initialized = false;
    if (vm) vm->DetachCurrentThread();
}

bool Java::AssignClass(std::string name, jclass &out)
{
    if (Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        throw std::runtime_error("[Lightrod] Incompatible Fusion mapping before " + name);
    }
    jstring className = Java::Env->NewStringUTF(name.c_str());
    jobject findClass = Java::Env->CallObjectMethod(classLoader, mid_findClass, className);
    Java::Env->DeleteLocalRef(className);

    if (findClass)
    {
        out = (jclass)findClass;
        return true;
    }
        
    if (Java::Env->ExceptionCheck()) Java::Env->ExceptionClear();
    throw std::runtime_error("[Lightrod] Missing Fusion class mapping: " + name);
}

jclass Java::findClass(JNIEnv* p_env, jvmtiEnv* p_tienv, const std::string& path)
{
    jint class_count = 0;
    jclass* classes = nullptr;
    jclass foundclass = nullptr;
    p_tienv->GetLoadedClasses(&class_count, &classes);
    for (int i = 0; i < class_count; ++i)
    {
        char* signature_buffer = nullptr;
        p_tienv->GetClassSignature(classes[i], &signature_buffer, nullptr);
        std::string signature = signature_buffer;
        p_tienv->Deallocate((unsigned char*)signature_buffer);
        signature = signature.substr(1);
        signature.pop_back();
        if (signature == path)
        {
            foundclass = (jclass)p_env->NewLocalRef(classes[i]);
        }
        p_env->DeleteLocalRef(classes[i]);
    }
    p_tienv->Deallocate((unsigned char*)classes);
    return foundclass;
}
