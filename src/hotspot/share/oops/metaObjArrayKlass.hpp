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

#ifndef SHARE_OOPS_METAOBJARRAYKLASS_HPP
#define SHARE_OOPS_METAOBJARRAYKLASS_HPP

#include "oops/arrayKlass.hpp"
#include "utilities/macros.hpp"

class ClassLoaderData;

// ObjArrayKlass is the klass for objArrays

class MetaObjArrayKlass : public ArrayKlass {
  friend class Deoptimization;
  friend class JVMCIVMStructs;
  friend class oopFactory;
  friend class VMStructs;

 public:
  static const KlassKind Kind = MetaObjArrayKlassKind;

 private:
  // If you add a new field that points to any metaspace object, you
  // must add this field to ObjArrayKlass::metaspace_pointers_do().
  Klass* _bottom_klass;             // The one-dimensional type (InstanceKlass or TypeArrayKlass)
 protected:
  Klass* _element_klass;            // The klass of the elements of this array type
  ObjArrayKlass* volatile _refined_klasses[VALID_PROPS_COUNT];

 protected:
  // Constructor
  MetaObjArrayKlass(int n, Klass* element_klass, Symbol* name, KlassKind kind, ArrayKlass::ArrayProperties props, markWord mw);
  static MetaObjArrayKlass* allocate_klass(ClassLoaderData* loader_data, int n, Klass* k, Symbol* name, ArrayKlass::ArrayProperties props, TRAPS);

  static ArrayDescription array_layout_selection(Klass* element, ArrayProperties properties);
  ObjArrayKlass* allocate_klass_with_properties(ArrayKlass::ArrayProperties props, TRAPS);
  objArrayOop allocate_instance(int length, ArrayProperties props, TRAPS);

   // Create array_name for element klass
  static Symbol* create_element_klass_array_name(JavaThread* current, Klass* element_klass);

 public:
  // For dummy objects
  MetaObjArrayKlass() {}

  virtual Klass* element_klass() const      { return _element_klass; }
  virtual void set_element_klass(Klass* k)  { _element_klass = k; }

  ObjArrayKlass* klass_with_properties(ArrayKlass::ArrayProperties properties, TRAPS);
  static ByteSize default_refined_array_klass_offset() {
    return byte_offset_of(MetaObjArrayKlass, _refined_klasses) + in_ByteSize(sizeof(ObjArrayKlass*) * DEFAULT);
  }

  // Compiler/Interpreter offset
  static ByteSize element_klass_offset() { return byte_offset_of(MetaObjArrayKlass, _element_klass); }

  Klass* bottom_klass() const       { return _bottom_klass; }
  void set_bottom_klass(Klass* k)   { _bottom_klass = k; }
  Klass** bottom_klass_addr()       { return &_bottom_klass; }

  ModuleEntry* module() const override;
  PackageEntry* package() const override;

  // Dispatched operation
  bool can_be_primary_super_slow() const override;
  GrowableArray<Klass*>* compute_secondary_supers(int num_extra_slots,
                                                  Array<InstanceKlass*>* transitive_interfaces) override;
  DEBUG_ONLY(bool is_metaObjArray_klass_slow() const override { return true; })
  size_t oop_size(oop obj) const override;

  // Allocation
  static MetaObjArrayKlass*
  allocate_metaObjArray_klass(ClassLoaderData *loader_data, int n,
                              Klass *element_klass, TRAPS);

  oop multi_allocate(int rank, jint* sizes, TRAPS) override;

  // Compute protection domain
  oop protection_domain() const override { return bottom_klass()->protection_domain(); }

  void metaspace_pointers_do(MetaspaceClosure* iter) override;

#if INCLUDE_CDS
  void remove_unshareable_info() override;
  void remove_java_mirror() override;
  void restore_unshareable_info(ClassLoaderData* loader_data, Handle protection_domain, TRAPS);
#endif

 public:
  static MetaObjArrayKlass* cast(Klass* k) {
    return const_cast<MetaObjArrayKlass*>(cast(const_cast<const Klass*>(k)));
  }

  static const MetaObjArrayKlass* cast(const Klass* k) {
    assert(k->is_metaObjArray_klass(), "cast to MetaObjArrayKlass");
    return static_cast<const MetaObjArrayKlass*>(k);
  }

  // Sizing
  static int header_size()                { return sizeof(MetaObjArrayKlass)/wordSize; }
  int size() const override               { return ArrayKlass::static_size(header_size()); }

  // Initialization (virtual from Klass)
  void initialize(TRAPS) override;

 public:
  u2 compute_modifier_flags() const override;

 public:
  // Printing
  void print_on(outputStream* st) const override;
  void print_value_on(outputStream* st) const override;

  void oop_print_value_on(oop obj, outputStream* st) override;
#ifndef PRODUCT
  void oop_print_on      (oop obj, outputStream* st) override;
#endif //PRODUCT

  const char* internal_name() const override;

  // Verification
  void verify_on(outputStream* st) override;

  void oop_verify_on(oop obj, outputStream* st) override;
};

#endif // SHARE_OOPS_METAOBJARRAYKLASS_HPP
