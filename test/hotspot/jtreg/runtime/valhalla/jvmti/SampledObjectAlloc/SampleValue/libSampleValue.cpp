/*
 * Copyright (c) 2026, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "jni.h"
#include "jvmti.h"
#include "jvmti_common.hpp"

#include <atomic>

extern "C" {

// SampledObjectAlloc event might be triggered on any thread
static std::atomic<jfieldID> v_id;
static std::atomic<jfieldID> c_id;
static std::atomic<jclass> container_clazz_ref{nullptr};
static std::atomic<jclass> value_clazz_ref{nullptr};

JNIEXPORT void JNICALL Java_SampleValue_setupJNI(JNIEnv *env, jclass clazz, jclass container_clazz, jclass value_clazz) {
  // Setup variables

  v_id.store(env->GetFieldID(container_clazz, "v", "LSampleValue$Value;"));
  c_id.store(env->GetStaticFieldID(clazz, "c", "LSampleValue$Container;"));
  container_clazz_ref.store((jclass)env->NewGlobalRef(container_clazz));
  value_clazz_ref.store((jclass)env->NewGlobalRef(value_clazz));
}

JNIEXPORT void JNICALL
SampledObjectAlloc(jvmtiEnv *jvmti, JNIEnv* env, jthread thread, jobject object, jclass object_klass, jlong size) {
  jclass value_clazz = value_clazz_ref.load();
  if (value_clazz != nullptr && env->IsSameObject(value_clazz, object_klass)) {
    jobject c2 = env->GetStaticObjectField(container_clazz_ref.load(), c_id.load());
    env->SetObjectField(c2, v_id.load(), object);
  }
}

jint Agent_Initialize(JavaVM *jvm, char *options, void *reserved) {
  jvmtiEnv* jvmti = nullptr;
  jvmtiCapabilities caps;
  jvmtiEventCallbacks callbacks;
  jvmtiError err;
  jint res;

  LOG("AGENT INIT");
  res = jvm->GetEnv((void **) &jvmti, JVMTI_VERSION_9);
  if (res != JNI_OK || jvmti == nullptr) {
    LOG("Wrong result of a valid call to GetEnv!\n");
    return JNI_ERR;
  }

  memset(&caps, 0, sizeof(caps));
  caps.can_generate_sampled_object_alloc_events = 1;
  if (jvmti->AddCapabilities(&caps) != JVMTI_ERROR_NONE) {
    return JNI_ERR;
  }

  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.SampledObjectAlloc = &SampledObjectAlloc;

  err = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
  check_jvmti_error(err, "SetEventCallbacks");

  err = jvmti->SetHeapSamplingInterval(0);
  check_jvmti_error(err, "SetHeapSamplingInterval");

  err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, nullptr);
  check_jvmti_error(err, "SetEventNotificationMode");

  return JNI_OK;
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM *jvm, char *options, void *reserved) {
  return Agent_Initialize(jvm, options, reserved);
}
}
