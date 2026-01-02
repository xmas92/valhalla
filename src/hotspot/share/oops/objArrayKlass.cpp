/*
 * Copyright (c) 1997, 2025, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

#include "cds/cdsConfig.hpp"
#include "classfile/moduleEntry.hpp"
#include "classfile/packageEntry.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/vmClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "memory/metadataFactory.hpp"
#include "memory/metaspaceClosure.hpp"
#include "memory/oopFactory.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "oops/arrayKlass.hpp"
#include "oops/flatArrayKlass.hpp"
#include "oops/inlineKlass.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.inline.hpp"
#include "oops/layoutKind.hpp"
#include "oops/markWord.hpp"
#include "oops/objArrayKlass.inline.hpp"
#include "oops/objArrayOop.inline.hpp"
#include "oops/oop.inline.hpp"
#include "oops/refArrayKlass.hpp"
#include "oops/symbol.hpp"
#include "runtime/arguments.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/macros.hpp"

Symbol* ObjArrayKlass::create_element_klass_array_name(JavaThread* current, Klass* element_klass) {
  ResourceMark rm(current);
  char* name_str = element_klass->name()->as_C_string();
  int len = element_klass->name()->utf8_length();
  char* new_str = NEW_RESOURCE_ARRAY_IN_THREAD(current, char, len + 4);
  int idx = 0;
  new_str[idx++] = JVM_SIGNATURE_ARRAY;
  if (element_klass->is_instance_klass()) { // it could be an array or simple type
    new_str[idx++] = JVM_SIGNATURE_CLASS;
  }
  memcpy(&new_str[idx], name_str, len * sizeof(char));
  idx += len;
  if (element_klass->is_instance_klass()) {
    new_str[idx++] = JVM_SIGNATURE_ENDCLASS;
  }
  new_str[idx] = '\0';
  return SymbolTable::new_symbol(new_str);
}

ObjArrayKlass::ObjArrayKlass(MetaObjArrayKlass* meta_klass, KlassKind kind, ArrayKlass::ArrayProperties props, markWord mk) :
ArrayKlass(meta_klass->name(), kind, props, mk), _meta_klass(meta_klass) {
  Klass* const element_klass = meta_klass->element_klass();
  const int n = meta_klass->dimension();
  set_dimension(n);
  set_element_klass(element_klass);
  set_properties(props);

  Klass* const bk = bottom_klass();
  assert(bk != nullptr && (bk->is_instance_klass() || bk->is_typeArray_klass()), "invalid bottom klass");
  set_class_loader_data(bk->class_loader_data());

  if (element_klass->is_array_klass()) {
    set_lower_dimension(ArrayKlass::cast(element_klass));
  }

  int lh = array_layout_helper(T_OBJECT);
  if (ArrayKlass::is_null_restricted(props)) {
    assert(n == 1, "Bytecode does not support null-free multi-dim");
    lh = layout_helper_set_null_free(lh);
#ifdef _LP64
    assert(prototype_header().is_null_free_array(), "sanity");
#endif
  }
  set_layout_helper(lh);
  assert(is_array_klass(), "sanity");
  assert(is_objArray_klass(), "sanity");
}

size_t ObjArrayKlass::oop_size(oop obj) const {
  // In this assert, we cannot safely access the Klass* with compact headers,
  // because size_given_klass() calls oop_size() on objects that might be
  // concurrently forwarded, which would overwrite the Klass*.
  assert(UseCompactObjectHeaders || obj->is_objArray(), "must be object array");
  // return objArrayOop(obj)->object_size();
  return obj->is_flatArray() ? flatArrayOop(obj)->object_size(layout_helper()) : refArrayOop(obj)->object_size();
}

oop ObjArrayKlass::multi_allocate(int rank, jint* sizes, TRAPS) {
  assert(is_refArray_klass() || is_flatArray_klass(), "Must be");
  int length = *sizes;
  ArrayKlass* ld_klass = lower_dimension();
  // If length < 0 allocate will throw an exception.
  objArrayOop array = allocate_instance(length, CHECK_NULL);
  objArrayHandle h_array (THREAD, array);
  if (rank > 1) {
    if (length != 0) {
      for (int index = 0; index < length; index++) {
        oop sub_array = ld_klass->multi_allocate(rank-1, &sizes[1], CHECK_NULL);
        h_array->obj_at_put(index, sub_array);
      }
    } else {
      // Since this array dimension has zero length, nothing will be
      // allocated, however the lower dimension values must be checked
      // for illegal values.
      for (int i = 0; i < rank - 1; ++i) {
        sizes += 1;
        if (*sizes < 0) {
          THROW_MSG_NULL(vmSymbols::java_lang_NegativeArraySizeException(), err_msg("%d", *sizes));
        }
      }
    }
  }
  return h_array();
}

void ObjArrayKlass::copy_array(arrayOop s, int src_pos, arrayOop d,
                               int dst_pos, int length, TRAPS) {
  assert(s->is_objArray(), "must be obj array");

  if (UseArrayFlattening) {
    if (d->is_flatArray()) {
      FlatArrayKlass::cast(d->klass())->copy_array(s, src_pos, d, dst_pos, length, THREAD);
      return;
    }
    if (s->is_flatArray()) {
      FlatArrayKlass::cast(s->klass())->copy_array(s, src_pos, d, dst_pos, length, THREAD);
      return;
    }
  }

  assert(s->is_refArray() && d->is_refArray(), "Must be");
  RefArrayKlass::cast(s->klass())->copy_array(s, src_pos, d, dst_pos, length, THREAD);
}

bool ObjArrayKlass::can_be_primary_super_slow() const {
  if (!bottom_klass()->can_be_primary_super())
    // array of interfaces
    return false;
  else
    return Klass::can_be_primary_super_slow();
}

GrowableArray<Klass*>* ObjArrayKlass::compute_secondary_supers(int num_extra_slots,
                                                               Array<InstanceKlass*>* transitive_interfaces) {
  assert(transitive_interfaces == nullptr, "sanity");
  // interfaces = { cloneable_klass, serializable_klass, elemSuper[], ... };
  const Array<Klass*>* elem_supers = element_klass()->secondary_supers();
  int num_elem_supers = elem_supers == nullptr ? 0 : elem_supers->length();
  int num_secondaries = num_extra_slots + 2 + num_elem_supers;
  if (num_secondaries == 2) {
    // Must share this for correct bootstrapping!
    set_secondary_supers(Universe::the_array_interfaces_array(),
                         Universe::the_array_interfaces_bitmap());
    return nullptr;
  } else {
    GrowableArray<Klass*>* secondaries = new GrowableArray<Klass*>(num_elem_supers+2);
    secondaries->push(vmClasses::Cloneable_klass());
    secondaries->push(vmClasses::Serializable_klass());
    for (int i = 0; i < num_elem_supers; i++) {
      Klass* elem_super = elem_supers->at(i);
      Klass* array_super = elem_super->array_klass_or_null();
      assert(array_super != nullptr, "must already have been created");
      secondaries->push(array_super);
    }
    return secondaries;
  }
}

void ObjArrayKlass::initialize(TRAPS) {
  bottom_klass()->initialize(THREAD);  // dispatches to either InstanceKlass or TypeArrayKlass
}

void ObjArrayKlass::metaspace_pointers_do(MetaspaceClosure* it) {
  ArrayKlass::metaspace_pointers_do(it);
  // Unclear if the back pointer is fine and/or required.
  it->push(&_meta_klass);
  it->push(&_element_klass);
}

#if INCLUDE_CDS
void ObjArrayKlass::restore_unshareable_info(ClassLoaderData* loader_data, Handle protection_domain, TRAPS) {
  ArrayKlass::restore_unshareable_info(loader_data, protection_domain, CHECK);
}

void ObjArrayKlass::remove_unshareable_info() {
  ArrayKlass::remove_unshareable_info();
}

void ObjArrayKlass::remove_java_mirror() {
  ArrayKlass::remove_java_mirror();
}
#endif // INCLUDE_CDS

u2 ObjArrayKlass::compute_modifier_flags() const {
  // The modifier for an objectArray is the same as its element
  assert (element_klass() != nullptr, "should be initialized");

  // Return the flags of the bottom element type.
  u2 element_flags = bottom_klass()->compute_modifier_flags();

  int identity_flag = (Arguments::is_valhalla_enabled()) ? JVM_ACC_IDENTITY : 0;

  return (element_flags & (JVM_ACC_PUBLIC | JVM_ACC_PRIVATE | JVM_ACC_PROTECTED))
                        | (identity_flag | JVM_ACC_ABSTRACT | JVM_ACC_FINAL);
}

ModuleEntry* ObjArrayKlass::module() const {
  assert(bottom_klass() != nullptr, "ObjArrayKlass returned unexpected null bottom_klass");
  // The array is defined in the module of its bottom class
  return bottom_klass()->module();
}

PackageEntry* ObjArrayKlass::package() const {
  assert(bottom_klass() != nullptr, "ObjArrayKlass returned unexpected null bottom_klass");
  return bottom_klass()->package();
}

// Printing

void ObjArrayKlass::print_on(outputStream* st) const {
#ifndef PRODUCT
  Klass::print_on(st);
  st->print(" - element klass: ");
  element_klass()->print_value_on(st);
  st->cr();
#endif //PRODUCT
}

void ObjArrayKlass::print_value_on(outputStream* st) const {
  assert(is_klass(), "must be klass");

  element_klass()->print_value_on(st);
  st->print("[]");
}

#ifndef PRODUCT

void ObjArrayKlass::oop_print_on(oop obj, outputStream* st) {
  ShouldNotReachHere();
}

#endif //PRODUCT

void ObjArrayKlass::oop_print_value_on(oop obj, outputStream* st) {
  ShouldNotReachHere();
}

const char* ObjArrayKlass::internal_name() const {
  return external_name();
}

// Verification

void ObjArrayKlass::verify_on(outputStream* st) {
  ArrayKlass::verify_on(st);
  guarantee(element_klass()->is_klass(), "should be klass");
  guarantee(bottom_klass()->is_klass(), "should be klass");
  Klass* bk = bottom_klass();
  guarantee(bk->is_instance_klass() || bk->is_typeArray_klass() || bk->is_flatArray_klass(),
            "invalid bottom klass");
}

void ObjArrayKlass::oop_verify_on(oop obj, outputStream* st) {
  ArrayKlass::oop_verify_on(obj, st);
  guarantee(obj->is_objArray(), "must be objArray");
  guarantee(obj->is_null_free_array() || (!is_null_free_array_klass()), "null-free klass but not object");
  objArrayOop oa = objArrayOop(obj);
  for(int index = 0; index < oa->length(); index++) {
    guarantee(oopDesc::is_oop_or_null(oa->obj_at(index)), "should be oop");
  }
}
