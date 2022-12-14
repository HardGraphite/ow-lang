#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "object_util.h"
#include <utilities/attributes.h>

struct ow_machine;
struct ow_module_obj;
struct ow_native_class_def;
struct ow_native_class_def_ex;
struct ow_object;
struct ow_symbol_obj;

/// Class object.
struct ow_class_obj;

/// Create an empty class.
struct ow_class_obj *ow_class_obj_new(struct ow_machine *om);
/// Initialize class object from a native definition. The class must be empty.
/// Parameter `super` is optional. If `finalizer` in parameter `def` is provided,
/// `super` must be object class.
void ow_class_obj_load_native_def(
	struct ow_machine *om, struct ow_class_obj *self,
	struct ow_class_obj *super,	const struct ow_native_class_def *def,
	struct ow_module_obj *func_mod);
/// Initialize class object from a native definition. The class must be empty.
/// Parameter `super` is optional. If `finalizer` or `gc_marker` in parameter
/// `def` is provided, `super` must be object class.
void ow_class_obj_load_native_def_ex(
	struct ow_machine *om, struct ow_class_obj *self,
	struct ow_class_obj *super,	const struct ow_native_class_def_ex *def,
	struct ow_module_obj *func_mod);
/// Delete all data.
void ow_class_obj_clear(struct ow_machine *om, struct ow_class_obj *self);
/// Get class name.
ow_static_forceinline struct ow_symbol_obj *ow_class_obj_name(const struct ow_class_obj *self);
/// Get super class. Return NULL if there is no super class.
ow_static_forceinline struct ow_class_obj *ow_class_obj_super(const struct ow_class_obj *self);
/// Get number of object fields, not including extended fields.
ow_static_forceinline size_t ow_class_obj_attribute_count(const struct ow_class_obj *self);
/// Get attribute index. If not exists, return -1.
size_t ow_class_obj_find_attribute(
	const struct ow_class_obj *self, const struct ow_symbol_obj *name);
/// Get method index by name. If not exists, return -1.
size_t ow_class_obj_find_method(
	const struct ow_class_obj *self, const struct ow_symbol_obj *name);
/// Get method by index. If not exists, return NULL.
struct ow_object *ow_class_obj_get_method(
	const struct ow_class_obj *self, size_t index);
/// Set method by index. If not exists, return false.
bool ow_class_obj_set_method(
	struct ow_class_obj *self, size_t index, struct ow_object *method);
/// Set or add method by name. Return its index.
size_t ow_class_obj_set_method_y(
	struct ow_class_obj *self, const struct ow_symbol_obj *name, struct ow_object *method);
/// Get static attribute by name. If not exists, return NULL.
struct ow_object *ow_class_obj_get_static(
	const struct ow_class_obj *self, const struct ow_symbol_obj *name);
/// Set or add static attribute by name.
void ow_class_obj_set_static(
	struct ow_class_obj *self, const struct ow_symbol_obj *name, struct ow_object *val);
/// Test if `derived_class` is derived from `self` or if both are the same.
ow_static_inline bool ow_class_obj_is_base(
	struct ow_class_obj *self, struct ow_class_obj *derived_class);

struct ow_class_obj_pub_info {
	size_t basic_field_count;
	size_t native_field_count;
	bool has_extra_fields;
	struct ow_class_obj *super_class; // optional
	struct ow_symbol_obj *class_name; // optional
	void (*finalizer)(struct ow_machine *, struct ow_object *); // optional
	void (*gc_marker)(struct ow_machine *, struct ow_object *); // optional
};

void _ow_class_obj_fini(struct ow_class_obj *self);

ow_static_forceinline const struct ow_class_obj_pub_info *ow_class_obj_pub_info(
		const struct ow_class_obj *self) {
	return (const struct ow_class_obj_pub_info *)
		((const unsigned char *)self + OW_OBJECT_SIZE);
}

ow_static_forceinline struct ow_symbol_obj *ow_class_obj_name(
		const struct ow_class_obj *self) {
	return ow_class_obj_pub_info(self)->class_name;
}

ow_static_forceinline struct ow_class_obj *ow_class_obj_super(
		const struct ow_class_obj *self) {
	return ow_class_obj_pub_info(self)->super_class;
}

ow_static_forceinline size_t ow_class_obj_attribute_count(
		const struct ow_class_obj *self) {
	return ow_class_obj_pub_info(self)->basic_field_count;
}

ow_static_inline bool ow_class_obj_is_base(
		struct ow_class_obj *self, struct ow_class_obj *derived_class) {
	while (1) {
		if (!derived_class)
			return false;
		if (derived_class == self)
			return true;
		derived_class = ow_class_obj_super(derived_class);
	}
}
