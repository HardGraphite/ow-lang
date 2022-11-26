#if !OW_EXPORT_API
#	error "`OW_EXPORT_API` is not true"
#endif // !OW_EXPORT_API

#include <ow.h>

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include <compiler/compiler.h>
#include <compiler/error.h>
#include <machine/globals.h>
#include <machine/invoke.h>
#include <machine/machine.h>
#include <machine/modmgr.h>
#include <objects/boolobj.h>
#include <objects/cfuncobj.h>
#include <objects/classes.h>
#include <objects/classobj.h>
#include <objects/exceptionobj.h>
#include <objects/floatobj.h>
#include <objects/funcobj.h>
#include <objects/intobj.h>
#include <objects/memory.h>
#include <objects/moduleobj.h>
#include <objects/stringobj.h>
#include <objects/symbolobj.h>
#include <utilities/attributes.h>
#include <utilities/stream.h>
#include <utilities/strings.h>
#include <utilities/unreachable.h>

#ifdef __GNUC__
__attribute__((cold))
#endif // __GNUC__
ow_noinline ow_nodiscard static struct ow_exception_obj *_make_error(
		struct ow_machine *om, struct ow_class_obj *exc_type, const char *fmt, ...) {
	va_list ap;
	char buf[64];
	va_start(ap, fmt);
	const int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	assert(n >= 0);

	ow_objmem_push_ngc(om);
	struct ow_object *const msg_o =
		ow_object_from(ow_string_obj_new(om, buf, (size_t)n));
	struct ow_exception_obj *const exc_o = ow_exception_new(om, exc_type, msg_o);
	ow_objmem_pop_ngc(om);

	return exc_o;
}

OW_API OW_NODISCARD ow_machine_t *ow_create(void) {
	return ow_machine_new();
}

OW_API void ow_destroy(ow_machine_t *om) {
	ow_machine_del(om);
}

OW_API void ow_push_nil(ow_machine_t *om) {
	*++om->callstack.regs.sp = om->globals->value_nil;
}

OW_API void ow_push_bool(ow_machine_t *om, bool val) {
	*++om->callstack.regs.sp =
		val ? om->globals->value_true : om->globals->value_false;
}

OW_API void ow_push_int(ow_machine_t *om, intmax_t val) {
	static_assert(sizeof val == sizeof(int64_t), "");
	*++om->callstack.regs.sp = ow_int_obj_or_smallint(om, val);
}

OW_API void ow_push_float(ow_machine_t *om, double val) {
	*++om->callstack.regs.sp = ow_object_from(ow_float_obj_new(om, val));
}

OW_API void ow_push_symbol(ow_machine_t *om, const char *str, size_t len) {
	assert(str || !len);
	*++om->callstack.regs.sp = ow_object_from(ow_symbol_obj_new(om, str, len));
}

OW_API void ow_push_string(ow_machine_t *om, const char *str, size_t len) {
	assert(str || !len);
	*++om->callstack.regs.sp = ow_object_from(ow_string_obj_new(om, str, len));
}

ow_noinline static int _make_module_from_stream(
		ow_machine_t *om, const char *file_name, struct ow_istream *source) {
	struct ow_compiler *const compiler = ow_compiler_new(om);
	struct ow_module_obj *const module = ow_module_obj_new(om);
	*++om->callstack.regs.sp = ow_object_from(module);
	struct ow_sharedstr *const file_name_ss = ow_sharedstr_new(file_name, (size_t)-1);
	const bool ok = ow_compiler_compile(compiler, source, file_name_ss, 0, module);
	if (ow_unlikely(!ok)) {
		struct ow_syntax_error *const err = ow_compiler_error(compiler);
		*om->callstack.regs.sp = ow_object_from(_make_error(
			om, NULL, "%s : %u:%u-%u:%u : %s\n", file_name,
			err->location.begin.line, err->location.begin.column,
			err->location.end.line, err->location.end.column,
			ow_sharedstr_data(err->message)));
	}
	ow_sharedstr_unref(file_name_ss);
	ow_compiler_del(compiler);
	return ok ? 0 : OW_ERR_FAIL;
}

OW_API int ow_make_module(
		ow_machine_t *om, const char *name, const void *src, int flags) {
	const int mode = flags & 0xf;
	if (mode == OW_MKMOD_NATIVE) {
		const struct ow_native_module_def *const mod_def = src;
		ow_unused_var(mod_def);
		return OW_ERR_NIMPL; // TODO: Load module from native def.
	} else if (mode == OW_MKMOD_FILE) {
		struct ow_istream *const stream = ow_istream_open(src);
		if (ow_unlikely(!stream)) {
			*++om->callstack.regs.sp = ow_object_from(
				_make_error(om, NULL, "cannot open file `%s'", src));
			return OW_ERR_FAIL;
		}
		const int status = _make_module_from_stream(om, src, stream);
		ow_istream_close(stream);
		return status;
	} else if (mode == OW_MKMOD_STRING) {
		struct ow_istream *const stream = ow_istream_open_mem(src, (size_t)-1);
		const int status = _make_module_from_stream(om, "<string>", stream);
		ow_istream_close(stream);
		return status;
	} else if (mode == OW_MKMOD_STDIN) {
		return _make_module_from_stream(om, "<stdin>", ow_istream_stdin());
	} else if (mode == OW_MKMOD_LOAD) {
		struct ow_module_obj *const mod =
			ow_module_manager_load(om->module_manager, name, 0);
		if (ow_unlikely(!mod)) {
			*++om->callstack.regs.sp = ow_object_from(
				_make_error(om, NULL, "cannot find module `%s'", name));
			return OW_ERR_FAIL;
		}
		*++om->callstack.regs.sp = ow_object_from(mod);
		return 0;
	} else {
		*++om->callstack.regs.sp = ow_object_from(ow_module_obj_new(om));
		return 0;
	}
}

OW_API int ow_load_local(ow_machine_t *om, int index) {
	if (ow_likely(index < 0)) {
		assert(om->callstack.frame_info_list.current);
		struct ow_object **const vp =
			om->callstack.frame_info_list.current->arg_list + (size_t)(-index);
		if (ow_unlikely(vp >= om->callstack.regs.fp))
			return OW_ERR_INDEX;
		*++om->callstack.regs.sp = *vp;
		return 0;
	} else if (ow_likely(index > 0)) {
		assert(om->callstack.regs.fp >= om->callstack._data);
		struct ow_object **const vp = om->callstack.regs.fp + (size_t)(index - 1);
		if (ow_unlikely(vp > om->callstack.regs.sp))
			return OW_ERR_INDEX;
		*++om->callstack.regs.sp = *vp;
		return 0;
	} else {
		return OW_ERR_INDEX;
	}
}

OW_API int ow_load_global(ow_machine_t *om, const char *name) {
	assert(name);
	assert(om->callstack.frame_info_list.current);
	assert(om->callstack.frame_info_list.current->arg_list - 1 >= om->callstack._data);
	struct ow_object *const func_o =
		om->callstack.frame_info_list.current->arg_list[-1];
	assert(!ow_smallint_check(func_o));
	struct ow_class_obj *const func_class = ow_object_class(func_o);
	struct ow_module_obj *module;
	if (func_class == om->builtin_classes->cfunc)
		module = ow_object_cast(func_o, struct ow_cfunc_obj)->module;
	else if (func_class == om->builtin_classes->func)
		module = ow_object_cast(func_o, struct ow_func_obj)->module;
	else
		return OW_ERR_INDEX;
	struct ow_symbol_obj *const name_o = ow_symbol_obj_new(om, name, (size_t)-1);
	struct ow_object *const v = ow_module_obj_get_global_y(module, name_o);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	*++om->callstack.regs.sp = v;
	return 0;
}

OW_API void ow_dup(ow_machine_t *om, size_t count) {
	assert(om->callstack.frame_info_list.current);
	struct ow_object *const v = *om->callstack.regs.sp;
	while (count--)
		*++om->callstack.regs.sp = v;
}

/// Get local variable or argument like `ow_load_local()`. Return top value if index is 0.
/// Return `NULL` if not exists.
static struct ow_object *_get_local(ow_machine_t *om, int index) {
	if (ow_likely(index < 0)) {
		assert(om->callstack.frame_info_list.current);
		struct ow_object **const vp =
			om->callstack.frame_info_list.current->arg_list + (size_t)(-index);
		if (ow_unlikely(vp >= om->callstack.regs.fp))
			return NULL;
		return *vp;
	} else if (ow_likely(index == 0)) {
		assert(om->callstack.regs.sp >= om->callstack._data);
		return *om->callstack.regs.sp;
	} else {
		assert(om->callstack.regs.sp >= om->callstack.regs.fp);
		struct ow_object **const vp = om->callstack.regs.fp + (size_t)(index - 1);
		if (ow_unlikely(vp > om->callstack.regs.sp))
			return NULL;
		return *vp;
	}
}

OW_API int ow_read_nil(ow_machine_t *om, int index) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_likely(ow_class_obj_is_base(
			om->builtin_classes->nil, ow_object_class(v))))
		return 0;
	return OW_ERR_TYPE;
}

OW_API int ow_read_bool(ow_machine_t *om, int index, bool *val_p) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v)) {
		return OW_ERR_INDEX;
	} else if (v == om->globals->value_true) {
		*val_p = true;
		return 0;
	} else if (v == om->globals->value_false) {
		*val_p = false;
		return 0;
	} else if (!ow_smallint_check(v) && ow_class_obj_is_base(
			om->builtin_classes->bool_, ow_object_class(v))) {
		*val_p = ow_bool_obj_value(ow_object_cast(v, struct ow_bool_obj));
		return 0;
	} else {
		return OW_ERR_TYPE;
	}
}

OW_API int ow_read_int(ow_machine_t *om, int index, intmax_t *val_p) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_likely(ow_smallint_check(v))) {
		*val_p = ow_smallint_from_ptr(v);
		return 0;
	}
	if (ow_likely(ow_class_obj_is_base(
			om->builtin_classes->int_, ow_object_class(v)))) {
		*val_p = ow_int_obj_value(ow_object_cast(v, struct ow_int_obj));
		return 0;
	}
	return OW_ERR_TYPE;
}

OW_API int ow_read_float(ow_machine_t *om, int index, double *val_p) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_likely(ow_class_obj_is_base(
			om->builtin_classes->float_, ow_object_class(v)))) {
		*val_p = ow_float_obj_value(ow_object_cast(v, struct ow_float_obj));
		return 0;
	}
	return OW_ERR_TYPE;
}

OW_API int ow_read_symbol(
		ow_machine_t *om, int index, const char **str_p, size_t *len_p) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_unlikely(!ow_class_obj_is_base(
			om->builtin_classes->symbol, ow_object_class(v))))
		return OW_ERR_TYPE;

	struct ow_symbol_obj *const sym = ow_object_cast(v, struct ow_symbol_obj);
	if (ow_likely(str_p))
		*str_p = ow_symbol_obj_data(sym);
	if (len_p)
		*len_p = ow_symbol_obj_size(sym);
	return 0;
}

OW_API int ow_read_string(
		ow_machine_t *om, int index, const char **str_p, size_t *len_p) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_unlikely(!ow_class_obj_is_base(
			om->builtin_classes->string, ow_object_class(v))))
		return OW_ERR_TYPE;

	struct ow_string_obj *const str_o = ow_object_cast(v, struct ow_string_obj);
	if (str_p)
		*str_p = ow_string_obj_flatten(om, str_o, len_p);
	else if (len_p)
		*len_p = ow_string_obj_size(str_o);
	return 0;
}

OW_API int ow_read_string_to(
		ow_machine_t *om, int index, char *buf, size_t buf_sz) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_unlikely(!ow_class_obj_is_base(
			om->builtin_classes->string, ow_object_class(v))))
		return OW_ERR_TYPE;

	struct ow_string_obj *const str_o = ow_object_cast(v, struct ow_string_obj);
	const size_t cp_n = ow_string_obj_copy(str_o, 0, (size_t)-1, buf, buf_sz);
	assert(cp_n <= (size_t)INT_MAX);
	if (cp_n < buf_sz) {
		buf[cp_n] = '\0';
		return (int)cp_n;
	}
	const size_t str_sz = ow_string_obj_size(str_o);
	if (ow_likely(cp_n == str_sz))
		return (int)cp_n;
	assert(cp_n == buf_sz && buf_sz < str_sz);
	return OW_ERR_FAIL;
}

OW_API int ow_read_exception(ow_machine_t *om, int index, int flags, ...) {
	struct ow_object *const v = _get_local(om, index);
	if (ow_unlikely(!v))
		return OW_ERR_INDEX;
	if (ow_unlikely(ow_smallint_check(v)))
		return OW_ERR_TYPE;
	if (ow_unlikely(!ow_class_obj_is_base(
			om->builtin_classes->exception, ow_object_class(v))))
		return OW_ERR_TYPE;

	const int flags_target = flags & 0xf0;
	struct ow_exception_obj *const exc_o =
		ow_object_cast(v, struct ow_exception_obj);
	if (flags_target == OW_RDEXC_PUSH) {
		*++om->callstack.regs.sp = ow_exception_obj_data(exc_o);
	} else {
		va_list ap;
		struct ow_iostream *out_stream;
		va_start(ap, flags);
		if (flags_target == OW_RDEXC_PRINT) {
			FILE *const fp = va_arg(ap, FILE *);
			out_stream = fp == stdout ? ow_iostream_stdout() : ow_iostream_stderr();
		} else if (flags_target == OW_RDEXC_TOBUF) {
			out_stream = ow_iostream_open_mem(256);
		} else {
			out_stream = NULL;
		}
		int pri_flags = 0;
		if (flags & OW_RDEXC_MSG)
			pri_flags |= 1;
		if (flags & OW_RDEXC_BT)
			pri_flags |= 2;
		ow_exception_obj_print(om, exc_o, out_stream, pri_flags);
		if (flags_target == OW_RDEXC_TOBUF) {
			const char *data[2];
			ow_istream_data(out_stream, data);
			const size_t data_size = (size_t)(data[1] - data[0]);
			char *const buf = va_arg(ap, char *);
			const size_t buf_size = va_arg(ap, size_t);
			memcpy(buf, data[0], buf_size > data_size ? data_size : buf_size);
			buf[buf_size > data_size ? data_size : buf_size - 1] = '\0';
			ow_iostream_close(out_stream);
		}
		va_end(ap);
	}

	return 0;
}

OW_API int ow_store_local(ow_machine_t *om, int index) {
	if (ow_likely(index < 0)) {
		assert(om->callstack.frame_info_list.current);
		struct ow_object **const vp =
			om->callstack.frame_info_list.current->arg_list + (size_t)(-index);
		if (ow_unlikely(vp >= om->callstack.regs.fp))
			return OW_ERR_INDEX;
		*vp = *om->callstack.regs.sp--;
		return 0;
	} else if (ow_likely(index > 0)) {
		assert(om->callstack.regs.fp >= om->callstack._data);
		struct ow_object **const vp = om->callstack.regs.fp + (size_t)(index - 1);
		if (ow_unlikely(vp > om->callstack.regs.sp))
			return OW_ERR_INDEX;
		*vp = *om->callstack.regs.sp--;
		return 0;
	} else {
		return OW_ERR_INDEX;
	}
}

OW_API int ow_store_global(ow_machine_t *om, const char *name) {
	assert(name);
	assert(om->callstack.frame_info_list.current);
	assert(om->callstack.frame_info_list.current->arg_list - 1 >= om->callstack._data);
	struct ow_object *const func_o =
		om->callstack.frame_info_list.current->arg_list[-1];
	assert(!ow_smallint_check(func_o));
	struct ow_class_obj *const func_class = ow_object_class(func_o);
	struct ow_module_obj *module;
	if (func_class == om->builtin_classes->cfunc)
		module = ow_object_cast(func_o, struct ow_cfunc_obj)->module;
	else if (func_class == om->builtin_classes->func)
		module = ow_object_cast(func_o, struct ow_func_obj)->module;
	else
		return OW_ERR_INDEX;
	struct ow_symbol_obj *const name_o = ow_symbol_obj_new(om, name, (size_t)-1);
	ow_module_obj_set_global_y(module, name_o, *om->callstack.regs.sp--);
	return 0;
}

OW_API int ow_drop(ow_machine_t *om, int count) {
	struct ow_object **const fp_m1 = om->callstack.regs.fp - 1;
	struct ow_object **new_sp = om->callstack.regs.sp - (size_t)(unsigned)count;
	if (new_sp < fp_m1)
		new_sp = fp_m1;
	om->callstack.regs.sp = new_sp;
	return (int)(new_sp - fp_m1);
}

OW_API int ow_invoke(ow_machine_t *om, int argc, int flags) {
	int status;
	struct ow_object *result;

	assert(argc >= 0);
	assert(om->callstack.regs.sp >= om->callstack.regs.fp + argc);

	const int mode = flags & 0xf;
	if (mode == 0) {
	do_invoke:
		status = ow_machine_invoke(om, argc, &result);
	} else if (mode == OW_IVK_METHOD) {
		struct ow_object *const name_o = *(om->callstack.regs.sp - argc);
		struct ow_object *const self_o = *(om->callstack.regs.sp - argc + 1);
		if (ow_unlikely(ow_smallint_check(name_o) ||
				ow_object_class(name_o) != om->builtin_classes->symbol)) {
			result = ow_object_from(_make_error(
				om, NULL, "%s is not a %s object", "method name", "Symbol"));
			status = OW_ERR_FAIL;
			goto end_invoke;
		}
		struct ow_class_obj *const self_class =
			ow_unlikely(ow_smallint_check(self_o)) ?
				om->builtin_classes->int_ : ow_object_class(self_o);
		const size_t method_index = ow_class_obj_find_method(
			self_class, ow_object_cast(name_o, struct ow_symbol_obj));
		if (ow_likely(method_index != (size_t)-1)) {
			*(om->callstack.regs.sp - argc) =
				ow_class_obj_get_method(self_class, method_index);
		} else {
			result = ow_object_from(_make_error(
				om, NULL, "`%s' object has not method `%s'",
				ow_symbol_obj_data(_ow_class_obj_pub_info(self_class)->class_name),
				ow_symbol_obj_data(ow_object_cast(name_o, struct ow_symbol_obj))));
			status = OW_ERR_FAIL;
			goto end_invoke;
		}
		goto do_invoke;
	} else if (mode == OW_IVK_MODULE) {
		struct ow_object *const mod_o = *(om->callstack.regs.sp - argc);
		if (ow_unlikely(ow_smallint_check(mod_o) ||
				ow_object_class(mod_o) != om->builtin_classes->module)) {
			result = ow_object_from(_make_error(
				om, NULL, "%s is not a %s object", "module", "Module"));
			status = OW_ERR_FAIL;
			goto end_invoke;
		}
		const bool call_main = flags & OW_IVK_MODMAIN;
		status = ow_machine_run(
			om, ow_object_cast(mod_o, struct ow_module_obj), call_main, &result);
	} else {
		ow_unreachable();
	}

end_invoke:
	if (ow_unlikely(status) || !(flags & OW_IVK_NORETVAL))
		*++om->callstack.regs.sp = result;

	assert(status == 0 || status == OW_ERR_FAIL);
	return status;
}
