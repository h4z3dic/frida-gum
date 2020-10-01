/*
 * Copyright (C) 2020 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumquickcore.h"

#include "gumffi.h"
#include "gumquickinterceptor.h"
#include "gumquickmacros.h"
#include "gumquickstalker.h"
#include "gumsourcemap.h"

#include <ffi.h>
#ifdef HAVE_PTRAUTH
# include <ptrauth.h>
#endif

typedef struct _GumQuickFlushCallback GumQuickFlushCallback;
typedef struct _GumQuickFFIFunctionParams GumQuickFFIFunctionParams;
typedef guint8 GumQuickSchedulingBehavior;
typedef guint8 GumQuickExceptionsBehavior;
typedef guint8 GumQuickCodeTraps;
typedef guint8 GumQuickReturnValueShape;
typedef struct _GumQuickFFIFunction GumQuickFFIFunction;
typedef struct _GumQuickNativeCallback GumQuickNativeCallback;

struct _GumQuickFlushCallback
{
  GumQuickFlushNotify func;
  GumQuickScript * script;
};

struct _GumQuickWeakRef
{
  guint id;
  JSValue callback;

  GumQuickCore * core;
};

struct _GumQuickScheduledCallback
{
  gint id;
  gboolean repeat;
  GumQuickHeapPtr func;
  GSource * source;

  GumQuickCore * core;
};

struct _GumQuickExceptionSink
{
  GumQuickHeapPtr callback;
  GumQuickCore * core;
};

struct _GumQuickMessageSink
{
  GumQuickHeapPtr callback;
  GumQuickCore * core;
};

struct _GumQuickFFIFunctionParams
{
  JSCFunction * invoke;
  GumQuickHeapPtr prototype;

  GCallback implementation;
  GumQuickHeapPtr return_type;
  GumQuickHeapPtr argument_types;
  const gchar * abi_name;
  GumQuickSchedulingBehavior scheduling;
  GumQuickExceptionsBehavior exceptions;
  GumQuickCodeTraps traps;
  GumQuickReturnValueShape return_shape;
};

enum _GumQuickSchedulingBehavior
{
  GUM_QUICK_SCHEDULING_COOPERATIVE,
  GUM_QUICK_SCHEDULING_EXCLUSIVE
};

enum _GumQuickExceptionsBehavior
{
  GUM_QUICK_EXCEPTIONS_STEAL,
  GUM_QUICK_EXCEPTIONS_PROPAGATE
};

enum _GumQuickCodeTraps
{
  GUM_QUICK_CODE_TRAPS_DEFAULT,
  GUM_QUICK_CODE_TRAPS_ALL
};

enum _GumQuickReturnValueShape
{
  GUM_QUICK_RETURN_PLAIN,
  GUM_QUICK_RETURN_DETAILED
};

struct _GumQuickFFIFunction
{
  GumQuickNativePointer parent;

  GCallback implementation;
  GumQuickSchedulingBehavior scheduling;
  GumQuickExceptionsBehavior exceptions;
  GumQuickCodeTraps traps;
  GumQuickReturnValueShape return_shape;
  ffi_cif cif;
  ffi_type ** atypes;
  gsize arglist_size;
  gboolean is_variadic;
  int nargs_fixed;
  ffi_abi abi;
  GSList * data;

  GumQuickCore * core;
};

struct _GumQuickNativeCallback
{
  GumQuickNativePointer parent;

  GumQuickHeapPtr func;
  ffi_closure * closure;
  ffi_cif cif;
  ffi_type ** atypes;
  GSList * data;

  GumQuickCore * core;
};

static void gum_quick_flush_callback_free (GumQuickFlushCallback * self);
static gboolean gum_quick_flush_callback_notify (GumQuickFlushCallback * self);

GUMJS_DECLARE_FUNCTION (gumjs_set_timeout)
GUMJS_DECLARE_FUNCTION (gumjs_set_interval)
GUMJS_DECLARE_FUNCTION (gumjs_clear_timer)
GUMJS_DECLARE_FUNCTION (gumjs_gc)
GUMJS_DECLARE_FUNCTION (gumjs_send)
GUMJS_DECLARE_FUNCTION (gumjs_set_unhandled_exception_callback)
GUMJS_DECLARE_FUNCTION (gumjs_set_incoming_message_callback)
GUMJS_DECLARE_FUNCTION (gumjs_wait_for_event)

GUMJS_DECLARE_GETTER (gumjs_frida_get_heap_size)
GUMJS_DECLARE_GETTER (gumjs_frida_get_source_map)
GUMJS_DECLARE_GETTER (gumjs_frida_objc_get_source_map)
GUMJS_DECLARE_GETTER (gumjs_frida_java_get_source_map)
GUMJS_DECLARE_FUNCTION (gumjs_frida_objc_load)
GUMJS_DECLARE_FUNCTION (gumjs_frida_java_load)

GUMJS_DECLARE_GETTER (gumjs_script_get_file_name)
GUMJS_DECLARE_GETTER (gumjs_script_get_source_map)
GUMJS_DECLARE_FUNCTION (gumjs_script_next_tick)
GUMJS_DECLARE_FUNCTION (gumjs_script_pin)
GUMJS_DECLARE_FUNCTION (gumjs_script_unpin)
GUMJS_DECLARE_FUNCTION (gumjs_script_set_global_access_handler)

GUMJS_DECLARE_FUNCTION (gumjs_weak_ref_bind)
GUMJS_DECLARE_FUNCTION (gumjs_weak_ref_unbind)

GUMJS_DECLARE_FINALIZER (gumjs_weak_ref_finalize)

GUMJS_DECLARE_CONSTRUCTOR (gumjs_int64_construct)
GUMJS_DECLARE_FINALIZER (gumjs_int64_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_int64_add)
GUMJS_DECLARE_FUNCTION (gumjs_int64_sub)
GUMJS_DECLARE_FUNCTION (gumjs_int64_and)
GUMJS_DECLARE_FUNCTION (gumjs_int64_or)
GUMJS_DECLARE_FUNCTION (gumjs_int64_xor)
GUMJS_DECLARE_FUNCTION (gumjs_int64_shr)
GUMJS_DECLARE_FUNCTION (gumjs_int64_shl)
GUMJS_DECLARE_FUNCTION (gumjs_int64_not)
GUMJS_DECLARE_FUNCTION (gumjs_int64_compare)
GUMJS_DECLARE_FUNCTION (gumjs_int64_to_number)
GUMJS_DECLARE_FUNCTION (gumjs_int64_to_string)
GUMJS_DECLARE_FUNCTION (gumjs_int64_to_json)
GUMJS_DECLARE_FUNCTION (gumjs_int64_value_of)

GUMJS_DECLARE_CONSTRUCTOR (gumjs_uint64_construct)
GUMJS_DECLARE_FINALIZER (gumjs_uint64_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_add)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_sub)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_and)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_or)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_xor)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_shr)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_shl)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_not)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_compare)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_to_number)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_to_string)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_to_json)
GUMJS_DECLARE_FUNCTION (gumjs_uint64_value_of)

GUMJS_DECLARE_CONSTRUCTOR (gumjs_native_pointer_construct)
GUMJS_DECLARE_FINALIZER (gumjs_native_pointer_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_is_null)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_add)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_sub)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_and)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_or)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_xor)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_shr)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_shl)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_not)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_sign)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_strip)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_blend)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_compare)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_to_int32)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_to_uint32)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_to_string)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_to_json)
GUMJS_DECLARE_FUNCTION (gumjs_native_pointer_to_match_pattern)

GUMJS_DECLARE_FUNCTION (gumjs_array_buffer_wrap)
GUMJS_DECLARE_FUNCTION (gumjs_array_buffer_unwrap)

GUMJS_DECLARE_FINALIZER (gumjs_native_resource_finalize)

GUMJS_DECLARE_FINALIZER (gumjs_kernel_resource_finalize)

GUMJS_DECLARE_CONSTRUCTOR (gumjs_native_function_construct)
GUMJS_DECLARE_FINALIZER (gumjs_native_function_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_native_function_invoke)
GUMJS_DECLARE_FUNCTION (gumjs_native_function_call)
GUMJS_DECLARE_FUNCTION (gumjs_native_function_apply)

GUMJS_DECLARE_CONSTRUCTOR (gumjs_system_function_construct)
GUMJS_DECLARE_FINALIZER (gumjs_system_function_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_system_function_invoke)
GUMJS_DECLARE_FUNCTION (gumjs_system_function_call)
GUMJS_DECLARE_FUNCTION (gumjs_system_function_apply)

static int gumjs_ffi_function_init (JSContext * ctx,
    const GumQuickFFIFunctionParams * params, GumQuickCore * core);
static void gum_quick_ffi_function_finalize (GumQuickFFIFunction * func);
static int gum_quick_ffi_function_invoke (GumQuickFFIFunction * self,
    JSContext * ctx, GCallback implementation, int argc,
    int argv_index);
static int gumjs_ffi_function_invoke (JSContext * ctx,
    const GumQuickArgs * args);
static int gumjs_ffi_function_call (JSContext * ctx, GumQuickHeapPtr type,
    const GumQuickArgs * args);
static int gumjs_ffi_function_apply (JSContext * ctx, GumQuickHeapPtr type,
    const GumQuickArgs * args);
static void gumjs_ffi_function_get (JSContext * ctx, GumQuickHeapPtr type,
    GumQuickHeapPtr receiver, GumQuickCore * core, GumQuickFFIFunction ** func,
    GCallback * implementation);

static void gum_quick_ffi_function_params_init (
    GumQuickFFIFunctionParams * params, JSCFunction * invoke,
    GumQuickHeapPtr prototype, GumQuickReturnValueShape return_shape,
    const GumQuickArgs * args);

static GumQuickSchedulingBehavior gum_quick_require_scheduling_behavior (
    JSContext * ctx, int index);
static GumQuickExceptionsBehavior gum_quick_require_exceptions_behavior (
    JSContext * ctx, int index);
static GumQuickCodeTraps gum_quick_require_code_traps (JSContext * ctx,
    int index);

GUMJS_DECLARE_CONSTRUCTOR (gumjs_native_callback_construct)
GUMJS_DECLARE_FINALIZER (gumjs_native_callback_finalize)
static void gum_quick_native_callback_finalize (
    GumQuickNativeCallback * func, gboolean heap_destruct);
static void gum_quick_native_callback_invoke (ffi_cif * cif,
    void * return_value, void ** args, void * user_data);

static GumQuickCpuContext * gumjs_cpu_context_from_this (JSValueConst this_val,
    GumQuickCore * core);
GUMJS_DECLARE_FINALIZER (gumjs_cpu_context_finalize)
static JSValue gumjs_cpu_context_set_register (GumQuickCpuContext * self,
    JSContext * ctx, JSValueConst value, gpointer * reg);

static JSValue gumjs_source_map_new (const gchar * json, GumQuickCore * core);
GUMJS_DECLARE_CONSTRUCTOR (gumjs_source_map_construct)
GUMJS_DECLARE_FINALIZER (gumjs_source_map_finalize)
GUMJS_DECLARE_FUNCTION (gumjs_source_map_resolve)

static GumQuickWeakRef * gum_quick_weak_ref_new (guint id, JSValue callback,
    GumQuickCore * core);
static void gum_quick_weak_ref_clear (GumQuickWeakRef * ref);

static JSValue gum_quick_core_schedule_callback (GumQuickCore * self,
    const GumQuickArgs * args, gboolean repeat);
static GumQuickScheduledCallback * gum_quick_core_try_steal_scheduled_callback (
    GumQuickCore * self, gint id);

static GumQuickScheduledCallback * gum_scheduled_callback_new (guint id,
    GumQuickHeapPtr func, gboolean repeat, GSource * source,
    GumQuickCore * core);
static void gum_scheduled_callback_free (GumQuickScheduledCallback * callback);
static gboolean gum_scheduled_callback_invoke (
    GumQuickScheduledCallback * self);

static GumQuickExceptionSink * gum_quick_exception_sink_new (
    GumQuickHeapPtr callback, GumQuickCore * core);
static void gum_quick_exception_sink_free (GumQuickExceptionSink * sink);
static void gum_quick_exception_sink_handle_exception (
    GumQuickExceptionSink * self, JSValueConst exception);

static GumQuickMessageSink * gum_quick_message_sink_new (
    GumQuickHeapPtr callback, GumQuickCore * core);
static void gum_quick_message_sink_free (GumQuickMessageSink * sink);
static void gum_quick_message_sink_post (GumQuickMessageSink * self,
    const gchar * message, GBytes * data, GumQuickScope * scope);

static gboolean gum_quick_get_ffi_type (JSContext * ctx, GumQuickHeapPtr value,
    ffi_type ** type, GSList ** data);
static gboolean gum_quick_get_ffi_abi (JSContext * ctx, const gchar * name,
    ffi_abi * abi);
static gboolean gum_quick_get_ffi_value (JSContext * ctx, int index,
    const ffi_type * type, GumQuickCore * core, GumFFIValue * value);
static void gum_quick_push_ffi_value (JSContext * ctx,
    const GumFFIValue * value, const ffi_type * type, GumQuickCore * core);

static const JSCFunctionListEntry gumjs_global_entries[] =
{
  JS_CFUNC_DEF ("_setTimeout", 2, gumjs_set_timeout),
  JS_CFUNC_DEF ("_setInterval", 2, gumjs_set_interval),
  JS_CFUNC_DEF ("clearTimeout", 1, gumjs_clear_timer),
  JS_CFUNC_DEF ("clearInterval", 1, gumjs_clear_timer),
  JS_CFUNC_DEF ("gc", 0, gumjs_gc),
  JS_CFUNC_DEF ("_send", 2, gumjs_send),
  JS_CFUNC_DEF ("_setUnhandledExceptionCallback", 1,
      gumjs_set_unhandled_exception_callback),
  JS_CFUNC_DEF ("_setIncomingMessageCallback", 1,
      gumjs_set_incoming_message_callback),
  JS_CFUNC_DEF ("_waitForEvent", 0, gumjs_wait_for_event),
};

static const JSCFunctionListEntry gumjs_frida_entries[] =
{
  JS_PROP_STRING_DEF ("version", FRIDA_VERSION, JS_PROP_C_W_E),
  JS_CGETSET_DEF ("heapSize", gumjs_frida_get_heap_size, NULL),
  JS_CGETSET_DEF ("sourceMap", gumjs_frida_get_source_map, NULL),
  JS_CGETSET_DEF ("_objcSourceMap", gumjs_frida_objc_get_source_map, NULL),
  JS_CGETSET_DEF ("_javaSourceMap", gumjs_frida_java_get_source_map, NULL),
  JS_CFUNC_DEF ("_loadObjC", 0, gumjs_frida_objc_load),
  JS_CFUNC_DEF ("_loadJava", 0, gumjs_frida_java_load),
};

static const JSCFunctionListEntry gumjs_script_entries[] =
{
  JS_PROP_STRING_DEF ("runtime", "QJS", JS_PROP_C_W_E),
  JS_CGETSET_DEF ("fileName", gumjs_script_get_file_name, NULL),
  JS_CGETSET_DEF ("sourceMap", gumjs_script_get_source_map, NULL),
  JS_CFUNC_DEF ("_nextTick", 1, gumjs_script_next_tick),
  JS_CFUNC_DEF ("pin", 0, gumjs_script_pin),
  JS_CFUNC_DEF ("unpin", 0, gumjs_script_unpin),
  JS_CFUNC_DEF ("setGlobalAccessHandler", 1,
      gumjs_script_set_global_access_handler),
};

static const JSCFunctionListEntry gumjs_weak_ref_module_entries[] =
{
  JS_CFUNC_DEF ("bind", 2, gumjs_weak_ref_bind),
  JS_CFUNC_DEF ("unbind", 1, gumjs_weak_ref_unbind),
};

static const JSClassDef gumjs_weak_ref_def =
{
  "WeakRef",
  gumjs_weak_ref_finalize
};

static const JSClassDef gumjs_int64_def =
{
  "Int64",
  gumjs_int64_finalize
};

static const JSCFunctionListEntry gumjs_int64_entries[] =
{
  JS_CFUNC_DEF ("add", 1, gumjs_int64_add),
  JS_CFUNC_DEF ("sub", 1, gumjs_int64_sub),
  JS_CFUNC_DEF ("and", 1, gumjs_int64_and),
  JS_CFUNC_DEF ("or", 1, gumjs_int64_or),
  JS_CFUNC_DEF ("xor", 1, gumjs_int64_xor),
  JS_CFUNC_DEF ("shr", 1, gumjs_int64_shr),
  JS_CFUNC_DEF ("shl", 1, gumjs_int64_shl),
  JS_CFUNC_DEF ("not", 1, gumjs_int64_not),
  JS_CFUNC_DEF ("compare", 1, gumjs_int64_compare),
  JS_CFUNC_DEF ("toNumber", 0, gumjs_int64_to_number),
  JS_CFUNC_DEF ("toString", 1, gumjs_int64_to_string),
  JS_CFUNC_DEF ("toJSON", 0, gumjs_int64_to_json),
  JS_CFUNC_DEF ("valueOf", 0, gumjs_int64_value_of),
};

static const JSClassDef gumjs_uint64_def =
{
  "UInt64",
  gumjs_uint64_finalize
};

static const JSCFunctionListEntry gumjs_uint64_entries[] =
{
  JS_CFUNC_DEF ("add", 1, gumjs_uint64_add),
  JS_CFUNC_DEF ("sub", 1, gumjs_uint64_sub),
  JS_CFUNC_DEF ("and", 1, gumjs_uint64_and),
  JS_CFUNC_DEF ("or", 1, gumjs_uint64_or),
  JS_CFUNC_DEF ("xor", 1, gumjs_uint64_xor),
  JS_CFUNC_DEF ("shr", 1, gumjs_uint64_shr),
  JS_CFUNC_DEF ("shl", 1, gumjs_uint64_shl),
  JS_CFUNC_DEF ("not", 1, gumjs_uint64_not),
  JS_CFUNC_DEF ("compare", 1, gumjs_uint64_compare),
  JS_CFUNC_DEF ("toNumber", 0, gumjs_uint64_to_number),
  JS_CFUNC_DEF ("toString", 1, gumjs_uint64_to_string),
  JS_CFUNC_DEF ("toJSON", 0, gumjs_uint64_to_json),
  JS_CFUNC_DEF ("valueOf", 0, gumjs_uint64_value_of),
};

static const JSClassDef gumjs_native_pointer_def =
{
  "NativePointer",
  gumjs_native_pointer_finalize
};

static const JSCFunctionListEntry gumjs_native_pointer_entries[] =
{
  JS_CFUNC_DEF ("isNull", 0, gumjs_native_pointer_is_null),
  JS_CFUNC_DEF ("add", 1, gumjs_native_pointer_add),
  JS_CFUNC_DEF ("sub", 1, gumjs_native_pointer_sub),
  JS_CFUNC_DEF ("and", 1, gumjs_native_pointer_and),
  JS_CFUNC_DEF ("or", 1, gumjs_native_pointer_or),
  JS_CFUNC_DEF ("xor", 1, gumjs_native_pointer_xor),
  JS_CFUNC_DEF ("shr", 1, gumjs_native_pointer_shr),
  JS_CFUNC_DEF ("shl", 1, gumjs_native_pointer_shl),
  JS_CFUNC_DEF ("not", 0, gumjs_native_pointer_not),
  JS_CFUNC_DEF ("sign", 2, gumjs_native_pointer_sign),
  JS_CFUNC_DEF ("strip", 1, gumjs_native_pointer_strip),
  JS_CFUNC_DEF ("blend", 1, gumjs_native_pointer_blend),
  JS_CFUNC_DEF ("compare", 1, gumjs_native_pointer_compare),
  JS_CFUNC_DEF ("toInt32", 0, gumjs_native_pointer_to_int32),
  JS_CFUNC_DEF ("toUInt32", 0, gumjs_native_pointer_to_uint32),
  JS_CFUNC_DEF ("toString", 1, gumjs_native_pointer_to_string),
  JS_CFUNC_DEF ("toJSON", 0, gumjs_native_pointer_to_json),
  JS_CFUNC_DEF ("toMatchPattern", 0, gumjs_native_pointer_to_match_pattern),
};

static const JSCFunctionListEntry gumjs_array_buffer_class_entries[] =
{
  JS_CFUNC_DEF ("wrap", 2, gumjs_array_buffer_wrap),
};

static const JSCFunctionListEntry gumjs_array_buffer_instance_entries[] =
{
  JS_CFUNC_DEF ("unwrap", 0, gumjs_array_buffer_unwrap),
};

static const JSClassDef gumjs_native_resource_def =
{
  "NativeResource",
  gumjs_native_resource_finalize
};

static const JSClassDef gumjs_kernel_resource_def =
{
  "KernelResource",
  gumjs_kernel_resource_finalize
};

static const JSClassDef gumjs_native_function_def =
{
  "NativeFunction",
  gumjs_native_function_finalize
};

static const JSCFunctionListEntry gumjs_native_function_entries[] =
{
  JS_CFUNC_DEF ("call", 0, gumjs_native_function_call),
  JS_CFUNC_DEF ("apply", 2, gumjs_native_function_apply),
};

static const JSClassDef gumjs_system_function_def =
{
  "SystemFunction",
  gumjs_system_function_finalize
};

static const JSCFunctionListEntry gumjs_system_function_entries[] =
{
  JS_CFUNC_DEF ("call", 0, gumjs_system_function_call),
  JS_CFUNC_DEF ("apply", 2, gumjs_system_function_apply),
};

static const JSClassDef gumjs_native_callback_def =
{
  "NativeCallback",
  gumjs_native_callback_finalize
};

static const JSClassDef gumjs_cpu_context_def =
{
  "CpuContext",
  gumjs_cpu_context_finalize
};

#define GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED(A, R) \
  GUMJS_DEFINE_GETTER (gumjs_cpu_context_get_##A) \
  { \
    GumQuickCpuContext * self = gumjs_cpu_context_from_this (this_val, core); \
    \
    return _gum_quick_native_pointer_new (GSIZE_TO_POINTER (self->handle->R), \
        core); \
  } \
  \
  GUMJS_DEFINE_SETTER (gumjs_cpu_context_set_##A) \
  { \
    GumQuickCpuContext * self = gumjs_cpu_context_from_this (this_val, core); \
    \
    return gumjs_cpu_context_set_register (self, ctx, value, \
        (gpointer *) &self->handle->R); \
  }
#define GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR(R) \
  GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (R, R)

#define GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED(A, R) \
  JS_CGETSET_DEF (G_STRINGIFY (A), gumjs_cpu_context_get_##R, \
      gumjs_cpu_context_set_##R)
#define GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR(R) \
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED (R, R)

#if defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 4
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (eax)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (ecx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (edx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (ebx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (esp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (ebp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (esi)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (edi)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (eip)
#elif defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 8
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rax)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rcx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rdx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rbx)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rsp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rbp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rsi)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rdi)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r8)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r9)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r10)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r11)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r12)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r13)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r14)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r15)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (rip)
#elif defined (HAVE_ARM)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (pc)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (sp)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r0, r[0])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r1, r[1])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r2, r[2])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r3, r[3])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r4, r[4])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r5, r[5])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r6, r[6])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (r7, r[7])

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r8)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r9)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r10)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r11)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (r12)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (lr)
#elif defined (HAVE_ARM64)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (pc)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (sp)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x0, x[0])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x1, x[1])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x2, x[2])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x3, x[3])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x4, x[4])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x5, x[5])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x6, x[6])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x7, x[7])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x8, x[8])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x9, x[9])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x10, x[10])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x11, x[11])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x12, x[12])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x13, x[13])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x14, x[14])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x15, x[15])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x16, x[16])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x17, x[17])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x18, x[18])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x19, x[19])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x20, x[20])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x21, x[21])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x22, x[22])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x23, x[23])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x24, x[24])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x25, x[25])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x26, x[26])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x27, x[27])
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR_ALIASED (x28, x[28])

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (fp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (lr)
#elif defined (HAVE_MIPS)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (pc)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (gp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (sp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (fp)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (ra)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (hi)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (lo)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (at)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (v0)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (v1)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (a0)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (a1)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (a2)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (a3)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t0)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t1)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t2)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t3)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t4)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t5)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t6)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t7)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t8)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (t9)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s0)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s1)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s2)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s3)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s4)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s5)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s6)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (s7)

GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (k0)
GUMJS_DEFINE_CPU_CONTEXT_ACCESSOR (k1)
#endif

static const JSCFunctionListEntry gumjs_cpu_context_entries[] =
{
#if defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 4
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED (pc, eip),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED (sp, esp),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (eax),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (ecx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (edx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (ebx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (esp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (ebp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (esi),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (edi),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (eip),
#elif defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 8
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED (pc, rip),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR_ALIASED (sp, rsp),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rax),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rcx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rdx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rbx),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rsp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rbp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rsi),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rdi),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r8),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r9),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r10),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r11),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r12),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r13),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r14),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r15),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (rip),
#elif defined (HAVE_ARM)
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (pc),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (sp),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r1),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r2),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r3),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r4),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r5),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r6),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r7),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r8),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r9),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r10),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r11),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (r12),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (lr),
#elif defined (HAVE_ARM64)
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (pc),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (sp),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x1),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x2),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x3),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x4),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x5),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x6),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x7),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x8),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x9),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x10),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x11),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x12),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x13),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x14),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x15),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x16),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x17),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x18),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x19),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x20),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x21),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x22),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x23),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x24),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x25),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x26),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x27),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (x28),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (fp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (lr),
#elif defined (HAVE_MIPS)
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (pc),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (gp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (sp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (fp),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (ra),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (hi),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (lo),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (at),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (v0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (v1),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (a0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (a1),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (a2),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (a3),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t1),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t2),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t3),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t4),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t5),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t6),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t7),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t8),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (t9),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s1),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s2),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s3),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s4),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s5),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s6),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (s7),

  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (k0),
  GUMJS_EXPORT_CPU_CONTEXT_ACCESSOR (k1),
#endif
};

static const JSClassDef gumjs_source_map_def =
{
  "SourceMap",
  gumjs_source_map_finalize
};

static const JSCFunctionListEntry gumjs_source_map_entries[] =
{
  JS_CFUNC_DEF ("_resolve", 0, gumjs_source_map_resolve),
};

void
_gum_quick_core_init (GumQuickCore * self,
                      GumQuickScript * script,
                      GRecMutex * mutex,
                      const gchar * runtime_source_map,
                      GumQuickInterceptor * interceptor,
                      GumQuickStalker * stalker,
                      GumQuickMessageEmitter message_emitter,
                      GumScriptScheduler * scheduler,
                      JSContext * ctx)
{
  JSRuntime * rt;
  JSValue global_obj, obj, proto, ctor;

  rt = JS_GetRuntime (ctx);

  global_obj = JS_GetGlobalObject (ctx);

  g_object_get (script, "backend", &self->backend, NULL);
  g_object_unref (self->backend);

  self->script = script;
  self->runtime_source_map = runtime_source_map;
  self->interceptor = interceptor;
  self->stalker = stalker;
  self->message_emitter = message_emitter;
  self->scheduler = scheduler;
  self->exceptor = gum_exceptor_obtain ();
  self->ctx = ctx;
  self->current_scope = NULL;

  self->mutex = mutex;
  self->usage_count = 0;
  self->mutex_depth = 0;
  self->flush_notify = NULL;

  self->event_loop = g_main_loop_new (
      gum_script_scheduler_get_js_context (scheduler), FALSE);
  g_mutex_init (&self->event_mutex);
  g_cond_init (&self->event_cond);
  self->event_count = 0;
  self->event_source_available = TRUE;

  self->weak_refs = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gum_quick_weak_ref_clear);
  self->next_weak_ref_id = 1;

  self->scheduled_callbacks = g_hash_table_new (NULL, NULL);
  self->next_callback_id = 1;

  _gum_quick_store_module_data (ctx, "core", self);

  JS_DefinePropertyValueStr (ctx, global_obj, "global", global_obj,
      JS_PROP_ENUMERABLE);

  JS_SetPropertyFunctionList (ctx, global_obj, gumjs_global_entries,
      G_N_ELEMENTS (gumjs_global_entries));

  obj = JS_NewObject (ctx);
  JS_DefinePropertyValueStr (ctx, global_obj, "Frida", obj, JS_PROP_C_W_E);
  JS_SetPropertyFunctionList (ctx, obj, gumjs_frida_entries,
      G_N_ELEMENTS (gumjs_frida_entries));
  JS_FreeValue (ctx, obj);

  obj = JS_NewObject (ctx);
  JS_DefinePropertyValueStr (ctx, global_obj, "Script", obj, JS_PROP_C_W_E);
  JS_SetPropertyFunctionList (ctx, obj, gumjs_script_entries,
      G_N_ELEMENTS (gumjs_script_entries));
  JS_FreeValue (ctx, obj);

  obj = JS_NewObject (ctx);
  JS_DefinePropertyValueStr (ctx, global_obj, "WeakRef", obj, JS_PROP_C_W_E);
  JS_SetPropertyFunctionList (ctx, obj, gumjs_weak_ref_module_entries,
      G_N_ELEMENTS (gumjs_weak_ref_module_entries));
  JS_FreeValue (ctx, obj);

  JS_NewClassID (&self->weak_ref_class);
  JS_NewClass (rt, self->weak_ref_class, &gumjs_weak_ref_def);

  JS_NewClassID (&self->int64_class);
  JS_NewClass (rt, self->int64_class, &gumjs_int64_def);
  proto = JS_NewObject (ctx);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_int64_entries,
      G_N_ELEMENTS (gumjs_int64_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_int64_construct,
      gumjs_int64_def.class_name, 1, JS_CFUNC_constructor, 0);
  self->int64_ctor = JS_DupValue (ctx, ctor);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->int64_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj, gumjs_int64_def.class_name, ctor,
      JS_PROP_C_W_E);

  JS_NewClassID (&self->uint64_class);
  JS_NewClass (rt, self->uint64_class, &gumjs_uint64_def);
  proto = JS_NewObject (ctx);
  self->uint64_proto = JS_DupValue (ctx, proto);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_uint64_entries,
      G_N_ELEMENTS (gumjs_uint64_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_uint64_construct,
      gumjs_uint64_def.class_name, 1, JS_CFUNC_constructor, 0);
  self->uint64_ctor = JS_DupValue (ctx, ctor);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->uint64_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj, gumjs_uint64_def.class_name, ctor,
      JS_PROP_C_W_E);

  JS_NewClassID (&self->native_pointer_class);
  JS_NewClass (rt, self->native_pointer_class, &gumjs_native_pointer_def);
  proto = JS_NewObject (ctx);
  self->native_pointer_proto = JS_DupValue (ctx, proto);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_native_pointer_entries,
      G_N_ELEMENTS (gumjs_native_pointer_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_native_pointer_construct,
      gumjs_native_pointer_def.class_name, 1, JS_CFUNC_constructor, 0);
  self->native_pointer_ctor = JS_DupValue (ctx, ctor);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->native_pointer_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj,
      gumjs_native_pointer_def.class_name, ctor, JS_PROP_C_W_E);

  obj = JS_GetPropertyStr (ctx, global_obj, "ArrayBuffer");
  JS_SetPropertyFunctionList (ctx, obj, gumjs_array_buffer_class_entries,
      G_N_ELEMENTS (gumjs_array_buffer_class_entries));
  proto = JS_GetPrototype (ctx, obj);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_array_buffer_instance_entries,
      G_N_ELEMENTS (gumjs_array_buffer_instance_entries));
  JS_FreeValue (ctx, proto);
  JS_FreeValue (ctx, obj);

  JS_NewClassID (&self->native_resource_class);
  JS_NewClass (rt, self->native_resource_class, &gumjs_native_resource_def);
  proto = JS_NewObjectProto (ctx, self->native_pointer_proto);
  JS_SetClassProto (ctx, self->native_resource_class, proto);

  JS_NewClassID (&self->kernel_resource_class);
  JS_NewClass (rt, self->kernel_resource_class, &gumjs_kernel_resource_def);
  proto = JS_NewObjectProto (ctx, self->uint64_proto);
  JS_SetClassProto (ctx, self->kernel_resource_class, proto);

  JS_NewClassID (&self->native_function_class);
  JS_NewClass (rt, self->native_function_class, &gumjs_native_function_def);
  proto = JS_NewObjectProto (ctx, self->native_pointer_proto);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_native_function_entries,
      G_N_ELEMENTS (gumjs_native_function_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_native_function_construct,
      gumjs_native_function_def.class_name, 3, JS_CFUNC_constructor, 0);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->native_function_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj,
      gumjs_native_function_def.class_name, ctor, JS_PROP_C_W_E);

  JS_NewClassID (&self->system_function_class);
  JS_NewClass (rt, self->system_function_class, &gumjs_system_function_def);
  proto = JS_NewObjectProto (ctx, self->native_pointer_proto);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_system_function_entries,
      G_N_ELEMENTS (gumjs_system_function_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_system_function_construct,
      gumjs_system_function_def.class_name, 3, JS_CFUNC_constructor, 0);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->system_function_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj,
      gumjs_system_function_def.class_name, ctor, JS_PROP_C_W_E);

  JS_NewClassID (&self->native_callback_class);
  JS_NewClass (rt, self->native_callback_class, &gumjs_native_callback_def);
  proto = JS_NewObjectProto (ctx, self->native_pointer_proto);
  ctor = JS_NewCFunction2 (ctx, gumjs_native_callback_construct,
      gumjs_native_callback_def.class_name, 3, JS_CFUNC_constructor, 0);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->native_callback_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj,
      gumjs_native_callback_def.class_name, ctor, JS_PROP_C_W_E);

  JS_NewClassID (&self->cpu_context_class);
  JS_NewClass (rt, self->cpu_context_class, &gumjs_cpu_context_def);
  proto = JS_NewObject (ctx);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_cpu_context_entries,
      G_N_ELEMENTS (gumjs_cpu_context_entries));
  JS_SetClassProto (ctx, self->cpu_context_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj, gumjs_cpu_context_def.class_name,
      ctor, JS_PROP_C_W_E);

  JS_NewClassID (&self->source_map_class);
  JS_NewClass (rt, self->source_map_class, &gumjs_source_map_def);
  proto = JS_NewObject (ctx);
  JS_SetPropertyFunctionList (ctx, proto, gumjs_source_map_entries,
      G_N_ELEMENTS (gumjs_source_map_entries));
  ctor = JS_NewCFunction2 (ctx, gumjs_source_map_construct,
      gumjs_source_map_def.class_name, 1, JS_CFUNC_constructor, 0);
  self->source_map_ctor = JS_DupValue (ctx, ctor);
  JS_SetConstructor (ctx, ctor, proto);
  JS_SetClassProto (ctx, self->source_map_class, proto);
  JS_DefinePropertyValueStr (ctx, global_obj, gumjs_source_map_def.class_name,
      ctor, JS_PROP_C_W_E);

  JS_FreeValue (ctx, global_obj);
}

gboolean
_gum_quick_core_flush (GumQuickCore * self,
                       GumQuickFlushNotify flush_notify)
{
  GHashTableIter iter;
  GumQuickScheduledCallback * callback;
  gboolean done;

  self->flush_notify = flush_notify;

  g_mutex_lock (&self->event_mutex);
  self->event_source_available = FALSE;
  g_cond_broadcast (&self->event_cond);
  g_mutex_unlock (&self->event_mutex);
  g_main_loop_quit (self->event_loop);

  if (self->usage_count > 1)
    return FALSE;

  g_hash_table_iter_init (&iter, self->scheduled_callbacks);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *) &callback))
  {
    _gum_quick_core_pin (self);
    g_source_destroy (callback->source);
  }
  g_hash_table_remove_all (self->scheduled_callbacks);

  if (self->usage_count > 1)
    return FALSE;

  g_hash_table_remove_all (self->weak_refs);

  done = self->usage_count == 1;
  if (done)
    self->flush_notify = NULL;

  return done;
}

static void
gum_quick_core_notify_flushed (GumQuickCore * self,
                               GumQuickFlushNotify func)
{
  GumQuickFlushCallback * callback;
  GSource * source;

  callback = g_slice_new (GumQuickFlushCallback);
  callback->func = func;
  callback->script = g_object_ref (self->script);

  source = g_idle_source_new ();
  g_source_set_callback (source, (GSourceFunc) gum_quick_flush_callback_notify,
      callback, (GDestroyNotify) gum_quick_flush_callback_free);
  g_source_attach (source,
      gum_script_scheduler_get_js_context (self->scheduler));
  g_source_unref (source);
}

static void
gum_quick_flush_callback_free (GumQuickFlushCallback * self)
{
  g_object_unref (self->script);

  g_slice_free (GumQuickFlushCallback, self);
}

static gboolean
gum_quick_flush_callback_notify (GumQuickFlushCallback * self)
{
  self->func (self->script);
  return FALSE;
}

void
_gum_quick_core_dispose (GumQuickCore * self)
{
  JSContext * ctx = self->ctx;

  g_clear_pointer (&self->unhandled_exception_sink,
      gum_quick_exception_sink_free);

  g_clear_pointer (&self->incoming_message_sink, gum_quick_message_sink_free);

  g_clear_pointer (&self->exceptor, g_object_unref);

  JS_FreeValue (ctx, self->int64_ctor);
  JS_FreeValue (ctx, self->uint64_ctor);
  JS_FreeValue (ctx, self->uint64_proto);
  JS_FreeValue (ctx, self->native_pointer_ctor);
  JS_FreeValue (ctx, self->native_pointer_proto);
  JS_FreeValue (ctx, self->source_map_ctor);
}

void
_gum_quick_core_finalize (GumQuickCore * self)
{
  g_hash_table_unref (self->scheduled_callbacks);
  self->scheduled_callbacks = NULL;

  g_hash_table_unref (self->weak_refs);
  self->weak_refs = NULL;

  g_main_loop_unref (self->event_loop);
  self->event_loop = NULL;
  g_mutex_clear (&self->event_mutex);
  g_cond_clear (&self->event_cond);

  g_assert (self->current_scope == NULL);
  self->ctx = NULL;
}

void
_gum_quick_core_pin (GumQuickCore * self)
{
  self->usage_count++;
}

void
_gum_quick_core_unpin (GumQuickCore * self)
{
  self->usage_count--;
}

void
_gum_quick_core_post (GumQuickCore * self,
                      const gchar * message,
                      GBytes * data)
{
  gboolean delivered = FALSE;
  GumQuickScope scope;

  _gum_quick_scope_enter (&scope, self);

  if (self->incoming_message_sink != NULL)
  {
    gum_quick_message_sink_post (self->incoming_message_sink, message, data,
        &scope);
    delivered = TRUE;
  }

  _gum_quick_scope_leave (&scope);

  if (delivered)
  {
    g_mutex_lock (&self->event_mutex);
    self->event_count++;
    g_cond_broadcast (&self->event_cond);
    g_mutex_unlock (&self->event_mutex);

    g_main_loop_quit (self->event_loop);
  }
  else
  {
    g_bytes_unref (data);
  }
}

void
_gum_quick_core_push_job (GumQuickCore * self,
                          GumScriptJobFunc job_func,
                          gpointer data,
                          GDestroyNotify data_destroy)
{
  gum_script_scheduler_push_job_on_thread_pool (self->scheduler, job_func,
      data, data_destroy);
}

void
_gum_quick_scope_enter (GumQuickScope * self,
                        GumQuickCore * core)
{
  self->core = core;

  g_rec_mutex_lock (core->mutex);

  gum_interceptor_begin_transaction (core->interceptor->interceptor);

  _gum_quick_core_pin (core);
  core->mutex_depth++;

  if (core->mutex_depth == 1)
  {
    g_assert (core->current_scope == NULL);
    core->current_scope = self;
  }

#if 0
  self->exception = NULL;
#endif

  g_queue_init (&self->tick_callbacks);
  g_queue_init (&self->scheduled_sources);

  self->pending_stalker_level = 0;
  self->pending_stalker_transformer = NULL;
  self->pending_stalker_sink = NULL;
}

void
_gum_quick_scope_suspend (GumQuickScope * self)
{
  GumQuickCore * core = self->core;
  guint i;

  gum_interceptor_end_transaction (core->interceptor->interceptor);

  g_assert (core->current_scope != NULL);
  self->previous_scope = g_steal_pointer (&core->current_scope);

  self->previous_mutex_depth = core->mutex_depth;
  core->mutex_depth = 0;

  for (i = 0; i != self->previous_mutex_depth; i++)
    g_rec_mutex_unlock (core->mutex);
}

void
_gum_quick_scope_resume (GumQuickScope * self)
{
  GumQuickCore * core = self->core;
  guint i;

  for (i = 0; i != self->previous_mutex_depth; i++)
    g_rec_mutex_lock (core->mutex);

  g_assert (core->current_scope == NULL);
  core->current_scope = g_steal_pointer (&self->previous_scope);

  core->mutex_depth = self->previous_mutex_depth;
  self->previous_mutex_depth = 0;

  gum_interceptor_begin_transaction (core->interceptor->interceptor);
}

gboolean
_gum_quick_scope_call (GumQuickScope * self,
                       JSValueConst func_obj,
                       JSValueConst this_obj,
                       int argc,
                       JSValueConst * argv)
{
  gboolean success;
  GumQuickCore * core = self->core;
  JSContext * ctx = core->ctx;
  JSValue result;

  result = JS_Call (ctx, func_obj, this_obj, argc, argv);

  success = !JS_IsException (result);
  if (success)
  {
    JS_FreeValue (ctx, result);
  }
  else
  {
    JSValue exception = JS_GetException (ctx);

    if (core->unhandled_exception_sink != NULL)
    {
      gum_quick_exception_sink_handle_exception (
          core->unhandled_exception_sink, exception);
    }

    JS_FreeValue (ctx, exception);
  }

  return success;
}

void
_gum_quick_scope_perform_pending_io (GumQuickScope * self)
{
#if 0
  JSContext * ctx = self->core->ctx;
  GumQuickHeapPtr tick_callback;
#endif
  GSource * source;

#if 0
  while ((tick_callback = g_queue_pop_head (&self->tick_callbacks)) != NULL)
  {
    quick_push_heapptr (ctx, tick_callback);
    _gum_quick_scope_call (self, 0);
    quick_pop (ctx);

    _gum_quick_unprotect (ctx, tick_callback);
  }
#endif

  while ((source = g_queue_pop_head (&self->scheduled_sources)) != NULL)
  {
    if (!g_source_is_destroyed (source))
    {
      g_source_attach (source,
          gum_script_scheduler_get_js_context (self->core->scheduler));
    }

    g_source_unref (source);
  }
}

void
_gum_quick_scope_leave (GumQuickScope * self)
{
  GumQuickCore * core = self->core;
  GumQuickFlushNotify pending_flush_notify = NULL;

  g_assert (core->current_scope == self);

  _gum_quick_scope_perform_pending_io (self);

  if (core->mutex_depth == 1)
  {
    core->current_scope = NULL;
  }

  core->mutex_depth--;
  _gum_quick_core_unpin (core);

  if (core->flush_notify != NULL && core->usage_count == 0)
  {
    pending_flush_notify = core->flush_notify;
    core->flush_notify = NULL;
  }

  gum_interceptor_end_transaction (self->core->interceptor->interceptor);

  g_rec_mutex_unlock (core->mutex);

  if (pending_flush_notify != NULL)
    gum_quick_core_notify_flushed (core, pending_flush_notify);

  _gum_quick_stalker_process_pending (core->stalker, self);
}

GUMJS_DEFINE_GETTER (gumjs_frida_get_heap_size)
{
  return JS_NewUint32 (ctx, gum_peek_private_memory_usage ());
}

GUMJS_DEFINE_GETTER (gumjs_frida_get_source_map)
{
  return gumjs_source_map_new (core->runtime_source_map, core);
}

GUMJS_DEFINE_GETTER (gumjs_frida_objc_get_source_map)
{
#if 0
  return gumjs_source_map_new (core->gumjs_objc_source_map, core);
#else
  return JS_NULL;
#endif
}

GUMJS_DEFINE_GETTER (gumjs_frida_java_get_source_map)
{
#if 0
  return gumjs_source_map_new (core->gumjs_java_source_map, core);
#else
  return JS_NULL;
#endif
}

GUMJS_DEFINE_FUNCTION (gumjs_frida_objc_load)
{
#if 0
  gum_quick_bundle_load (gumjs_objc_modules, ctx);
#endif

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_frida_java_load)
{
#if 0
  gum_quick_bundle_load (gumjs_java_modules, ctx);
#endif

  return JS_UNDEFINED;
}

GUMJS_DEFINE_GETTER (gumjs_script_get_file_name)
{
  JSValue result;
  gchar * name, * file_name;

  g_object_get (core->script, "name", &name, NULL);
  file_name = g_strconcat ("/", name, ".js", NULL);
  result = JS_NewString (ctx, file_name);
  g_free (file_name);
  g_free (name);

  return result;
}

GUMJS_DEFINE_GETTER (gumjs_script_get_source_map)
{
  JSValue result;
  gchar * source;
  GRegex * regex;
  GMatchInfo * match_info;

  g_object_get (core->script, "source", &source, NULL);

  if (source == NULL)
    return JS_NULL;

  regex = g_regex_new ("//[#@][ \\t]sourceMappingURL=[ \\t]*"
      "data:application/json;.*?base64,([^\\s'\"]*)[ \\t]*$",
      G_REGEX_MULTILINE, 0, NULL);
  g_regex_match (regex, source, 0, &match_info);
  if (g_match_info_matches (match_info))
  {
    gchar * data_encoded;
    gsize size;
    gchar * data;

    data_encoded = g_match_info_fetch (match_info, 1);

    data = (gchar *) g_base64_decode (data_encoded, &size);
    if (data != NULL && g_utf8_validate (data, size, NULL))
    {
      gchar * data_utf8;

      data_utf8 = g_strndup (data, size);
      result = gumjs_source_map_new (data_utf8, core);
      g_free (data_utf8);
    }
    else
    {
      result = JS_NULL;
    }
    g_free (data);

    g_free (data_encoded);
  }
  else
  {
    result = JS_NULL;
  }
  g_match_info_free (match_info);
  g_regex_unref (regex);

  g_free (source);

  return result;
}

GUMJS_DEFINE_FUNCTION (gumjs_script_next_tick)
{
  GumQuickHeapPtr callback;

  if (!_gum_quick_args_parse (args, "F", &callback))
    return JS_EXCEPTION;

#if 0
  _gum_quick_protect (ctx, callback);
  g_queue_push_tail (&args->core->current_scope->tick_callbacks, callback);
#endif

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_script_pin)
{
  _gum_quick_core_pin (args->core);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_script_unpin)
{
  _gum_quick_core_unpin (args->core);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_script_set_global_access_handler)
{
  /* TODO */
  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_weak_ref_bind)
{
  GumQuickCore * core = args->core;
  JSValue target;
  JSValue callback;
  gboolean target_is_valid;
  guint id;
  gchar prop_name[4 + 8 + 2 + 1];
  GumQuickWeakRef * ref;
  JSValue obj;

  if (!_gum_quick_args_parse (args, "VF", &target, &callback))
    return JS_EXCEPTION;

  target_is_valid = JS_IsObject (target);
  if (!target_is_valid)
    return _gum_quick_throw_literal (ctx, "expected a heap value");

  id = core->next_weak_ref_id++;

  ref = gum_quick_weak_ref_new (id, callback, core);
  g_hash_table_insert (core->weak_refs, GUINT_TO_POINTER (id), ref);

  obj = JS_NewObjectClass (ctx, core->weak_ref_class);
  JS_SetOpaque (obj, ref);

  sprintf (prop_name, "__wr%x__", id);
  JS_DefinePropertyValueStr (ctx, target, prop_name, obj, 0);

  return JS_NewInt32 (ctx, id);
}

GUMJS_DEFINE_FUNCTION (gumjs_weak_ref_unbind)
{
  guint id;
  gboolean removed;

  if (!_gum_quick_args_parse (args, "u", &id))
    return JS_EXCEPTION;

  removed = !g_hash_table_remove (args->core->weak_refs, GUINT_TO_POINTER (id));

  return JS_NewBool (ctx, removed);
}

GUMJS_DEFINE_FINALIZER (gumjs_weak_ref_finalize)
{
  GumQuickWeakRef * self;

  self = JS_GetOpaque (val, core->weak_ref_class);

  if (self->core != NULL)
  {
    g_hash_table_remove (self->core->weak_refs, GUINT_TO_POINTER (self->id));
  }

  g_slice_free (GumQuickWeakRef, self);
}

GUMJS_DEFINE_FUNCTION (gumjs_set_timeout)
{
  GumQuickCore * self = args->core;

  return gum_quick_core_schedule_callback (self, args, FALSE);
}

GUMJS_DEFINE_FUNCTION (gumjs_set_interval)
{
  GumQuickCore * self = args->core;

  return gum_quick_core_schedule_callback (self, args, TRUE);
}

GUMJS_DEFINE_FUNCTION (gumjs_clear_timer)
{
  GumQuickCore * self = args->core;
  gint id;
  GumQuickScheduledCallback * callback;

  if (!JS_IsNumber (args->argv[0]))
    goto invalid_handle;

  if (!_gum_quick_args_parse (args, "i", &id))
    return JS_EXCEPTION;

  callback = gum_quick_core_try_steal_scheduled_callback (self, id);
  if (callback != NULL)
  {
    _gum_quick_core_pin (self);
    g_source_destroy (callback->source);
  }

  return JS_NewBool (ctx, callback != NULL);

invalid_handle:
  {
    return JS_NewBool (ctx, FALSE);
  }
}

GUMJS_DEFINE_FUNCTION (gumjs_gc)
{
  JS_RunGC (JS_GetRuntime (ctx));

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_send)
{
  GumQuickCore * self = args->core;
  GumInterceptor * interceptor = self->interceptor->interceptor;
  const gchar * message;
  GBytes * data;

  if (!_gum_quick_args_parse (args, "sB?", &message, &data))
    return JS_EXCEPTION;

  /*
   * Synchronize Interceptor state before sending the message. The application
   * might be waiting for an acknowledgement that APIs have been instrumented.
   *
   * This is very important for the RPC API.
   */
  gum_interceptor_end_transaction (interceptor);
  gum_interceptor_begin_transaction (interceptor);

  self->message_emitter (self->script, message, data);

  g_bytes_unref (data);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_set_unhandled_exception_callback)
{
  GumQuickCore * self = args->core;
  GumQuickHeapPtr callback;
  GumQuickExceptionSink * new_sink, * old_sink;

  if (!_gum_quick_args_parse (args, "F?", &callback))
    return JS_EXCEPTION;

  new_sink = (callback != NULL)
      ? gum_quick_exception_sink_new (callback, self)
      : NULL;

  old_sink = self->unhandled_exception_sink;
  self->unhandled_exception_sink = new_sink;

  if (old_sink != NULL)
    gum_quick_exception_sink_free (old_sink);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_set_incoming_message_callback)
{
  GumQuickCore * self = args->core;
  GumQuickHeapPtr callback;
  GumQuickMessageSink * new_sink, * old_sink;

  if (!_gum_quick_args_parse (args, "F?", &callback))
    return JS_EXCEPTION;

  new_sink = (callback != NULL)
      ? gum_quick_message_sink_new (callback, self)
      : NULL;

  old_sink = self->incoming_message_sink;
  self->incoming_message_sink = new_sink;

  if (old_sink != NULL)
    gum_quick_message_sink_free (old_sink);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_wait_for_event)
{
  GumQuickCore * self = args->core;
  GumQuickScope scope = GUM_QUICK_SCOPE_INIT (self);
  GMainContext * context;
  gboolean called_from_js_thread;
  guint start_count;
  gboolean event_source_available;

  _gum_quick_scope_perform_pending_io (self->current_scope);

  _gum_quick_scope_suspend (&scope);

  context = gum_script_scheduler_get_js_context (self->scheduler);
  called_from_js_thread = g_main_context_is_owner (context);

  g_mutex_lock (&self->event_mutex);

  start_count = self->event_count;
  while (self->event_count == start_count && self->event_source_available)
  {
    if (called_from_js_thread)
    {
      g_mutex_unlock (&self->event_mutex);
      g_main_loop_run (self->event_loop);
      g_mutex_lock (&self->event_mutex);
    }
    else
    {
      g_cond_wait (&self->event_cond, &self->event_mutex);
    }
  }

  event_source_available = self->event_source_available;

  g_mutex_unlock (&self->event_mutex);

  _gum_quick_scope_resume (&scope);

  if (!event_source_available)
    return _gum_quick_throw_literal (ctx, "script is unloading");

  return JS_UNDEFINED;
}

static gboolean
gumjs_int64_from_this (JSValueConst this_val,
                       GumQuickCore * core,
                       gint64 * value)
{
  GumQuickInt64 * self;

  self = JS_GetOpaque (this_val, core->int64_class);
  if (self == NULL)
    goto invalid_type;

  *value = self->value;
  return TRUE;

invalid_type:
  {
    JS_ThrowTypeError (core->ctx, "not an Int64");
    return FALSE;
  }
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_int64_construct)
{
  gint64 value;
  GumQuickInt64 * self;

  if (JS_IsNull (this_val))
  {
    return _gum_quick_throw_literal (ctx, "use `new Int64()` to create a new "
        "instance, or use the shorthand: `int64()`");
  }

  if (!_gum_quick_args_parse (args, "q~", &value))
    return JS_EXCEPTION;

  self = g_slice_new (GumQuickInt64);
  self->value = value;

  JS_SetOpaque (this_val, self);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FINALIZER (gumjs_int64_finalize)
{
  GumQuickInt64 * self;

  self = JS_GetOpaque (val, core->int64_class);
  if (self == NULL)
    return;

  g_slice_free (GumQuickInt64, self);
}

#define GUM_DEFINE_INT64_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_int64_##name) \
  { \
    GumQuickCore * core = args->core; \
    gint64 lhs, rhs, result; \
    \
    if (!gumjs_int64_from_this (this_val, core, &lhs)) \
      return JS_EXCEPTION; \
    \
    if (!_gum_quick_args_parse (args, "q~", &rhs)) \
      return JS_EXCEPTION; \
    \
    result = lhs op rhs; \
    \
    return _gum_quick_int64_new (result, core); \
  }

GUM_DEFINE_INT64_OP_IMPL (add, +)
GUM_DEFINE_INT64_OP_IMPL (sub, -)
GUM_DEFINE_INT64_OP_IMPL (and, &)
GUM_DEFINE_INT64_OP_IMPL (or,  |)
GUM_DEFINE_INT64_OP_IMPL (xor, ^)
GUM_DEFINE_INT64_OP_IMPL (shr, >>)
GUM_DEFINE_INT64_OP_IMPL (shl, <<)

#define GUM_DEFINE_INT64_UNARY_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_int64_##name) \
  { \
    GumQuickCore * core = args->core; \
    gint64 value, result; \
    \
    if (!gumjs_int64_from_this (this_val, core, &value)) \
      return JS_EXCEPTION; \
    \
    result = op value; \
    \
    return _gum_quick_int64_new (result, core); \
  }

GUM_DEFINE_INT64_UNARY_OP_IMPL (not, ~)

GUMJS_DEFINE_FUNCTION (gumjs_int64_compare)
{
  gint64 lhs, rhs;
  gint result;

  if (!gumjs_int64_from_this (this_val, args->core, &lhs))
    return JS_EXCEPTION;

  if (!_gum_quick_args_parse (args, "q~", &rhs))
    return JS_EXCEPTION;

  result = (lhs == rhs) ? 0 : ((lhs < rhs) ? -1 : 1);

  return JS_NewInt32 (ctx, result);
}

GUMJS_DEFINE_FUNCTION (gumjs_int64_to_number)
{
  gint64 value;

  if (!gumjs_int64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  return JS_NewInt64 (ctx, value);
}

GUMJS_DEFINE_FUNCTION (gumjs_int64_to_string)
{
  gint64 value;
  gint radix;
  gchar str[32];

  if (!gumjs_int64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  radix = 10;
  if (!_gum_quick_args_parse (args, "|u", &radix))
    return JS_EXCEPTION;
  if (radix != 10 && radix != 16)
    return _gum_quick_throw_literal (ctx, "unsupported radix");

  if (radix == 10)
    sprintf (str, "%" G_GINT64_FORMAT, value);
  else if (value >= 0)
    sprintf (str, "%" G_GINT64_MODIFIER "x", value);
  else
    sprintf (str, "-%" G_GINT64_MODIFIER "x", -value);

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_int64_to_json)
{
  gint64 value;
  gchar str[32];

  if (!gumjs_int64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  sprintf (str, "%" G_GINT64_FORMAT, value);

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_int64_value_of)
{
  gint64 value;

  if (!gumjs_int64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  return JS_NewInt64 (ctx, value);
}

static gboolean
gumjs_uint64_from_this (JSValueConst this_val,
                        GumQuickCore * core,
                        guint64 * value)
{
  GumQuickUInt64 * self;

  self = JS_GetOpaque (this_val, core->uint64_class);
  if (self == NULL)
    goto invalid_type;

  *value = self->value;
  return TRUE;

invalid_type:
  {
    JS_ThrowTypeError (core->ctx, "not a UInt64");
    return FALSE;
  }
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_uint64_construct)
{
  guint64 value;
  GumQuickUInt64 * self;

  if (JS_IsNull (this_val))
  {
    return _gum_quick_throw_literal (ctx, "use `new UInt64()` to create a new "
        "instance, or use the shorthand: `uint64()`");
  }

  if (!_gum_quick_args_parse (args, "Q~", &value))
    return JS_EXCEPTION;

  self = g_slice_new (GumQuickUInt64);
  self->value = value;

  JS_SetOpaque (this_val, self);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FINALIZER (gumjs_uint64_finalize)
{
  GumQuickUInt64 * self;

  self = JS_GetOpaque (val, core->uint64_class);
  if (self == NULL)
    return;

  g_slice_free (GumQuickUInt64, self);
}

#define GUM_DEFINE_UINT64_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_uint64_##name) \
  { \
    GumQuickCore * core = args->core; \
    guint64 lhs, rhs, result; \
    \
    if (!gumjs_uint64_from_this (this_val, core, &lhs)) \
      return JS_EXCEPTION; \
    \
    if (!_gum_quick_args_parse (args, "Q~", &rhs)) \
      return JS_EXCEPTION; \
    \
    result = lhs op rhs; \
    \
    return _gum_quick_uint64_new (result, core); \
  }

GUM_DEFINE_UINT64_OP_IMPL (add, +)
GUM_DEFINE_UINT64_OP_IMPL (sub, -)
GUM_DEFINE_UINT64_OP_IMPL (and, &)
GUM_DEFINE_UINT64_OP_IMPL (or,  |)
GUM_DEFINE_UINT64_OP_IMPL (xor, ^)
GUM_DEFINE_UINT64_OP_IMPL (shr, >>)
GUM_DEFINE_UINT64_OP_IMPL (shl, <<)

#define GUM_DEFINE_UINT64_UNARY_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_uint64_##name) \
  { \
    GumQuickCore * core = args->core; \
    guint64 value, result; \
    \
    if (!gumjs_uint64_from_this (this_val, core, &value)) \
      return JS_EXCEPTION; \
    \
    result = op value; \
    \
    return _gum_quick_uint64_new (result, core); \
  }

GUM_DEFINE_UINT64_UNARY_OP_IMPL (not, ~)

GUMJS_DEFINE_FUNCTION (gumjs_uint64_compare)
{
  guint64 lhs, rhs;
  gint result;

  if (!gumjs_uint64_from_this (this_val, args->core, &lhs))
    return JS_EXCEPTION;

  if (!_gum_quick_args_parse (args, "Q~", &rhs))
    return JS_EXCEPTION;

  result = (lhs == rhs) ? 0 : ((lhs < rhs) ? -1 : 1);

  return JS_NewInt32 (ctx, result);
}

GUMJS_DEFINE_FUNCTION (gumjs_uint64_to_number)
{
  guint64 value;

  if (!gumjs_uint64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  return JS_NewInt64 (ctx, value);
}

GUMJS_DEFINE_FUNCTION (gumjs_uint64_to_string)
{
  guint64 value;
  gint radix;
  gchar str[32];

  if (!gumjs_uint64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  radix = 10;
  if (!_gum_quick_args_parse (args, "|u", &radix))
    return JS_EXCEPTION;
  if (radix != 10 && radix != 16)
    return _gum_quick_throw_literal (ctx, "unsupported radix");

  if (radix == 10)
    sprintf (str, "%" G_GUINT64_FORMAT, value);
  else
    sprintf (str, "%" G_GINT64_MODIFIER "x", value);

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_uint64_to_json)
{
  guint64 value;
  gchar str[32];

  if (!gumjs_uint64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  sprintf (str, "%" G_GUINT64_FORMAT, value);

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_uint64_value_of)
{
  guint64 value;

  if (!gumjs_uint64_from_this (this_val, args->core, &value))
    return JS_EXCEPTION;

  return JS_NewInt64 (ctx, value);
}

static gboolean
gumjs_native_pointer_from_this (JSValueConst this_val,
                                GumQuickCore * core,
                                GumQuickNativePointer ** ptr)
{
  GumQuickNativePointer * self;

  self = JS_GetOpaque (this_val, core->native_pointer_class);
  if (self == NULL)
    goto invalid_type;

  *ptr = self;
  return TRUE;

invalid_type:
  {
    JS_ThrowTypeError (core->ctx, "not a NativePointer");
    return FALSE;
  }
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_native_pointer_construct)
{
  gpointer ptr = NULL;
  GumQuickNativePointer * self;

  if (JS_IsNull (this_val))
  {
    return _gum_quick_throw_literal (ctx, "use `new NativePointer()` to create "
        "a new instance, or use one of the two shorthands: `ptr()` and `NULL`");
  }

  if (!_gum_quick_args_parse (args, "p~", &ptr))
    return JS_EXCEPTION;

  self = g_slice_new0 (GumQuickNativePointer);
  self->value = ptr;

  JS_SetOpaque (this_val, self);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FINALIZER (gumjs_native_pointer_finalize)
{
  GumQuickNativePointer * self;

  self = JS_GetOpaque (val, core->native_pointer_class);
  if (self == NULL)
    return;

  g_slice_free (GumQuickNativePointer, self);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_is_null)
{
  GumQuickNativePointer * self;

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  return JS_NewBool (ctx, self->value == NULL);
}

#define GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_##name) \
  { \
    GumQuickCore * core = args->core; \
    GumQuickNativePointer * self; \
    gpointer rhs_ptr; \
    gsize lhs, rhs; \
    gpointer result; \
    \
    if (!gumjs_native_pointer_from_this (this_val, core, &self)) \
      return JS_EXCEPTION; \
    \
    if (!_gum_quick_args_parse (args, "p~", &rhs_ptr)) \
      return JS_EXCEPTION; \
    \
    lhs = GPOINTER_TO_SIZE (self->value); \
    rhs = GPOINTER_TO_SIZE (rhs_ptr); \
    \
    result = GSIZE_TO_POINTER (lhs op rhs); \
    \
    return _gum_quick_native_pointer_new (result, core); \
  }

GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (add, +)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (sub, -)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (and, &)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (or,  |)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (xor, ^)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (shr, >>)
GUM_DEFINE_NATIVE_POINTER_BINARY_OP_IMPL (shl, <<)

#define GUM_DEFINE_NATIVE_POINTER_UNARY_OP_IMPL(name, op) \
  GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_##name) \
  { \
    GumQuickCore * core = args->core; \
    GumQuickNativePointer * self; \
    gpointer result; \
    \
    if (!gumjs_native_pointer_from_this (this_val, core, &self)) \
      return JS_EXCEPTION; \
    \
    result = GSIZE_TO_POINTER (op GPOINTER_TO_SIZE (self->value)); \
    \
    return _gum_quick_native_pointer_new (result, core); \
  }

GUM_DEFINE_NATIVE_POINTER_UNARY_OP_IMPL (not, ~)

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_sign)
{
#ifdef HAVE_PTRAUTH
  GumQuickCore * core = args->core;
  GumQuickNativePointer * self;
  gpointer value;
  const gchar * key;
  gpointer data;

  if (!gumjs_native_pointer_from_this (this_val, core, &self))
    return JS_EXCEPTION;

  value = self->value;

  key = "ia";
  data = NULL;
  if (!_gum_quick_args_parse (args, "|sp~", &key, &data))
    return JS_EXCEPTION;

  if (strcmp (key, "ia") == 0)
    value = ptrauth_sign_unauthenticated (value, ptrauth_key_asia, data);
  else if (strcmp (key, "ib") == 0)
    value = ptrauth_sign_unauthenticated (value, ptrauth_key_asib, data);
  else if (strcmp (key, "da") == 0)
    value = ptrauth_sign_unauthenticated (value, ptrauth_key_asda, data);
  else if (strcmp (key, "db") == 0)
    value = ptrauth_sign_unauthenticated (value, ptrauth_key_asdb, data);
  else
    return _gum_quick_throw_literal (ctx, "invalid key");

  return _gum_quick_native_pointer_new (value, core);
#else
  return JS_DupValue (ctx, this_val);
#endif
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_strip)
{
#ifdef HAVE_PTRAUTH
  GumQuickCore * core = args->core;
  GumQuickNativePointer * self;
  gpointer value;
  const gchar * key;

  if (!gumjs_native_pointer_from_this (this_val, core, &self))
    return JS_EXCEPTION;

  value = self->value;

  key = "ia";
  if (!_gum_quick_args_parse (args, "|s", &key))
    return JS_EXCEPTION;

  if (strcmp (key, "ia") == 0)
    value = ptrauth_strip (value, ptrauth_key_asia);
  else if (strcmp (key, "ib") == 0)
    value = ptrauth_strip (value, ptrauth_key_asib);
  else if (strcmp (key, "da") == 0)
    value = ptrauth_strip (value, ptrauth_key_asda);
  else if (strcmp (key, "db") == 0)
    value = ptrauth_strip (value, ptrauth_key_asdb);
  else
    return _gum_quick_throw_literal (ctx, "invalid key");

  return _gum_quick_native_pointer_new (value, core);
#else
  return JS_DupValue (ctx, this_val);
#endif
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_blend)
{
#ifdef HAVE_PTRAUTH
  GumQuickCore * core = args->core;
  GumQuickNativePointer * self;
  gpointer value;
  guint small_integer;

  if (!gumjs_native_pointer_from_this (this_val, core, &self))
    return JS_EXCEPTION;

  value = self->value;

  if (!_gum_quick_args_parse (args, "u", &small_integer))
    return JS_EXCEPTION;

  value = GSIZE_TO_POINTER (ptrauth_blend_discriminator (value, small_integer));

  return _gum_quick_native_pointer_new (value, core);
#else
  return JS_DupValue (ctx, this_val);
#endif
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_compare)
{
  GumQuickNativePointer * self;
  gpointer rhs_ptr;
  gsize lhs, rhs;
  gint result;

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  if (!_gum_quick_args_parse (args, "p~", &rhs_ptr))
    return JS_EXCEPTION;

  lhs = GPOINTER_TO_SIZE (self->value);
  rhs = GPOINTER_TO_SIZE (rhs_ptr);

  result = (lhs == rhs) ? 0 : ((lhs < rhs) ? -1 : 1);

  return JS_NewInt32 (ctx, result);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_to_int32)
{
  GumQuickNativePointer * self;
  gint32 result;

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  result = (gint32) GPOINTER_TO_SIZE (self->value);

  return JS_NewInt32 (ctx, result);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_to_uint32)
{
  GumQuickNativePointer * self;
  guint32 result;

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  result = (guint32) GPOINTER_TO_SIZE (self->value);

  return JS_NewUint32 (ctx, result);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_to_string)
{
  GumQuickNativePointer * self;
  gint radix = 0;
  gboolean radix_specified;
  gsize ptr;
  gchar str[32];

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  if (!_gum_quick_args_parse (args, "|u", &radix))
    return JS_EXCEPTION;

  radix_specified = radix != 0;
  if (!radix_specified)
    radix = 16;
  else if (radix != 10 && radix != 16)
    return _gum_quick_throw_literal (ctx, "unsupported radix");

  ptr = GPOINTER_TO_SIZE (self->value);

  if (radix == 10)
  {
    sprintf (str, "%" G_GSIZE_MODIFIER "u", ptr);
  }
  else
  {
    if (radix_specified)
      sprintf (str, "%" G_GSIZE_MODIFIER "x", ptr);
    else
      sprintf (str, "0x%" G_GSIZE_MODIFIER "x", ptr);
  }

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_to_json)
{
  GumQuickNativePointer * self;
  gchar str[32];

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  sprintf (str, "0x%" G_GSIZE_MODIFIER "x", GPOINTER_TO_SIZE (self->value));

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_pointer_to_match_pattern)
{
  GumQuickNativePointer * self;
  gsize ptr;
  gchar str[24];
  gint src, dst;
  const gint num_bits = GLIB_SIZEOF_VOID_P * 8;
  const gchar nibble_to_char[] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
      'a', 'b', 'c', 'd', 'e', 'f'
  };

  if (!gumjs_native_pointer_from_this (this_val, args->core, &self))
    return JS_EXCEPTION;

  ptr = GPOINTER_TO_SIZE (self->value);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  for (src = 0, dst = 0; src != num_bits; src += 8)
#else
  for (src = num_bits - 8, dst = 0; src >= 0; src -= 8)
#endif
  {
    if (dst != 0)
      str[dst++] = ' ';
    str[dst++] = nibble_to_char[(ptr >> (src + 4)) & 0xf];
    str[dst++] = nibble_to_char[(ptr >> (src + 0)) & 0xf];
  }
  str[dst] = '\0';

  return JS_NewString (ctx, str);
}

GUMJS_DEFINE_FUNCTION (gumjs_array_buffer_wrap)
{
  gpointer address;
  gsize size;

  if (!_gum_quick_args_parse (args, "pZ", &address, &size))
    return JS_EXCEPTION;

  return JS_NewArrayBuffer (ctx, address, size, NULL, NULL, FALSE);
}

GUMJS_DEFINE_FUNCTION (gumjs_array_buffer_unwrap)
{
  uint8_t * address;
  size_t size;

  address = JS_GetArrayBuffer (ctx, &size, this_val);
  if (address == NULL)
    return JS_EXCEPTION;

  return _gum_quick_native_pointer_new (address, args->core);
}

GUMJS_DEFINE_FINALIZER (gumjs_native_resource_finalize)
{
  GumQuickNativeResource * self;

  self = JS_GetOpaque (val, core->native_resource_class);
  if (self == NULL)
    return;

  if (self->notify != NULL)
    self->notify (self->parent.value);

  g_slice_free (GumQuickNativeResource, self);
}

GUMJS_DEFINE_FINALIZER (gumjs_kernel_resource_finalize)
{
  GumQuickKernelResource * self;

  self = JS_GetOpaque (val, core->kernel_resource_class);
  if (self == NULL)
    return;

  if (self->notify != NULL)
    self->notify (self->parent.value);

  g_slice_free (GumQuickKernelResource, self);
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_native_function_construct)
{
  GumQuickCore * core = args->core;
  GumQuickFFIFunctionParams params;

  if (JS_IsNull (this_val))
  {
    return _gum_quick_throw_literal (ctx,
        "use `new NativeFunction()` to create a new instance");
  }

  gum_quick_ffi_function_params_init (&params, gumjs_native_function_invoke,
      core->native_function_prototype, GUM_QUICK_RETURN_PLAIN, args);

  return gumjs_ffi_function_init (ctx, &params, core);
}

GUMJS_DEFINE_FINALIZER (gumjs_native_function_finalize)
{
  GumQuickFFIFunction * self;

  self = JS_GetOpaque (val, core->native_function_class);
  if (self == NULL)
    return;

  gum_quick_ffi_function_finalize (self);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_function_invoke)
{
  return gumjs_ffi_function_invoke (ctx, args);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_function_call)
{
  return gumjs_ffi_function_call (ctx, args->core->native_function, args);
}

GUMJS_DEFINE_FUNCTION (gumjs_native_function_apply)
{
  return gumjs_ffi_function_apply (ctx, args->core->native_function, args);
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_system_function_construct)
{
  GumQuickCore * core = args->core;
  GumQuickFFIFunctionParams params;

  if (!quick_is_constructor_call (ctx))
  {
    return _gum_quick_throw_literal (ctx,
        "use `new SystemFunction()` to create a new instance");
  }

  gum_quick_ffi_function_params_init (&params, gumjs_system_function_invoke,
      core->system_function_prototype, GUM_QUICK_RETURN_DETAILED, args);

  return gumjs_ffi_function_init (ctx, &params, core);
}

GUMJS_DEFINE_FINALIZER (gumjs_system_function_finalize)
{
  GumQuickFFIFunction * self;

  self = _gum_quick_steal_data (ctx, 0);
  if (self == NULL)
    return JS_UNDEFINED;

  gum_quick_ffi_function_finalize (self);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_system_function_invoke)
{
  return gumjs_ffi_function_invoke (ctx, args);
}

GUMJS_DEFINE_FUNCTION (gumjs_system_function_call)
{
  return gumjs_ffi_function_call (ctx, args->core->system_function, args);
}

GUMJS_DEFINE_FUNCTION (gumjs_system_function_apply)
{
  return gumjs_ffi_function_apply (ctx, args->core->system_function, args);
}

static int
gumjs_ffi_function_init (JSContext * ctx,
                         const GumQuickFFIFunctionParams * params,
                         GumQuickCore * core)
{
  GumQuickFFIFunction * func;
  GumQuickNativePointer * ptr;
  ffi_type * rtype;
  int nargs_fixed, nargs_total, length, i;
  gboolean is_variadic;
  ffi_abi abi;

  func = g_slice_new0 (GumQuickFFIFunction);
  ptr = &func->parent;
  ptr->value = GUM_FUNCPTR_TO_POINTER (params->implementation);
  func->implementation = params->implementation;
  func->scheduling = params->scheduling;
  func->exceptions = params->exceptions;
  func->traps = params->traps;
  func->return_shape = params->return_shape;
  func->core = core;

  if (!gum_quick_get_ffi_type (ctx, params->return_type, &rtype, &func->data))
    goto invalid_return_type;

  quick_push_heapptr (ctx, params->argument_types);

  length = quick_get_length (ctx, -1);
  nargs_fixed = nargs_total = length;
  is_variadic = FALSE;

  func->atypes = g_new (ffi_type *, nargs_total);

  for (i = 0; i != nargs_total; i++)
  {
    gboolean is_marker;

    quick_get_prop_index (ctx, -1, (quick_uarridx_t) i);

    if (quick_is_string (ctx, -1))
      is_marker = strcmp (quick_require_string (ctx, -1), "...") == 0;
    else
      is_marker = FALSE;

    if (is_marker)
    {
      if (i == 0 || is_variadic)
        goto unexpected_marker;

      nargs_fixed = i;
      is_variadic = TRUE;
    }
    else
    {
      GumQuickHeapPtr atype_value;
      ffi_type ** atype;

      atype_value = quick_get_heapptr (ctx, -1);
      atype = &func->atypes[is_variadic ? i - 1 : i];

      if (!gum_quick_get_ffi_type (ctx, atype_value, atype, &func->data))
        goto invalid_argument_type;

      if (is_variadic)
        *atype = gum_ffi_maybe_promote_variadic (*atype);
    }

    quick_pop (ctx);
  }

  quick_pop (ctx);

  if (is_variadic)
    nargs_total--;

  if (params->abi_name != NULL)
  {
    if (!gum_quick_get_ffi_abi (ctx, params->abi_name, &abi))
      goto invalid_abi;
  }
  else
  {
    abi = FFI_DEFAULT_ABI;
  }

  if (is_variadic)
  {
    if (ffi_prep_cif_var (&func->cif, abi, (guint) nargs_fixed,
        (guint) nargs_total, rtype, func->atypes) != FFI_OK)
      goto compilation_failed;
  }
  else
  {
    if (ffi_prep_cif (&func->cif, abi, (guint) nargs_total, rtype,
        func->atypes) != FFI_OK)
      goto compilation_failed;
  }

  func->is_variadic = nargs_fixed < nargs_total;
  func->nargs_fixed = nargs_fixed;
  func->abi = abi;

  for (i = 0; i != nargs_total; i++)
  {
    ffi_type * t = func->atypes[i];

    func->arglist_size = GUM_ALIGN_SIZE (func->arglist_size, t->alignment);
    func->arglist_size += t->size;
  }

  quick_push_c_function (ctx, params->invoke, QUICK_VARARGS);

  _gum_quick_put_data (ctx, -1, func);

  /* bound_func = func.bind(func); */
  quick_push_string (ctx, "bind");
  quick_dup (ctx, -2);
  quick_call_prop (ctx, -3, 1);
  quick_swap (ctx, -2, -1);
  quick_pop (ctx);

  /* `bound_func instanceof {Native,System}Function` should be true */
  quick_push_heapptr (ctx, params->prototype);
  quick_set_prototype (ctx, -2);

  /* `bound_func` needs the private data to be used as a NativePointer */
  _gum_quick_put_data (ctx, -1, func);

  /* we ignore `this` and return `bound_func` instead */
  return 1;

invalid_return_type:
  {
    gum_quick_ffi_function_finalize (func);
    return _gum_quick_throw_literal (ctx, "invalid return type");
  }
invalid_argument_type:
  {
    gum_quick_ffi_function_finalize (func);
    return _gum_quick_throw_literal (ctx, "invalid argument type");
  }
invalid_abi:
  {
    gum_quick_ffi_function_finalize (func);
    return _gum_quick_throw_literal (ctx, "invalid abi");
  }
unexpected_marker:
  {
    gum_quick_ffi_function_finalize (func);
    return _gum_quick_throw_literal (ctx, "only one variadic marker may be specified, "
        "and can not be the first argument");
  }
compilation_failed:
  {
    gum_quick_ffi_function_finalize (func);
    return _gum_quick_throw_literal (ctx, "failed to compile function call interface");
  }

  g_assert_not_reached ();
  return JS_UNDEFINED;
}

static void
gum_quick_ffi_function_finalize (GumQuickFFIFunction * func)
{
  while (func->data != NULL)
  {
    GSList * head = func->data;
    g_free (head->data);
    func->data = g_slist_delete_link (func->data, head);
  }
  g_free (func->atypes);

  g_slice_free (GumQuickFFIFunction, func);
}

static int
gum_quick_ffi_function_invoke (GumQuickFFIFunction * self,
                             JSContext * ctx,
                             GCallback implementation,
                             int argc,
                             int argv_index)
{
  GumQuickCore * core;
  ffi_cif * cif;
  gsize nargs, nargs_fixed;
  gboolean is_variadic;
  ffi_type * rtype;
  ffi_type ** atypes;
  gsize rsize, ralign;
  GumFFIValue * rvalue;
  void ** avalue;
  guint8 * avalues;
  ffi_cif tmp_cif;
  GumFFIValue tmp_value = { 0, };
  GumQuickSchedulingBehavior scheduling;
  GumQuickExceptionsBehavior exceptions;
  GumQuickCodeTraps traps;
  GumQuickReturnValueShape return_shape;
  GumExceptorScope exceptor_scope;
  GumInvocationState invocation_state;
  gint system_error;

  core = self->core;
  cif = &self->cif;
  nargs = cif->nargs;
  nargs_fixed = self->nargs_fixed;
  is_variadic = self->is_variadic;

  if ((is_variadic && argc < nargs_fixed) || (!is_variadic && argc != nargs))
    return _gum_quick_throw_literal (ctx, "bad argument count");

  rtype = cif->rtype;
  atypes = cif->arg_types;
  rsize = MAX (rtype->size, sizeof (gsize));
  ralign = MAX (rtype->alignment, sizeof (gsize));
  rvalue = g_alloca (rsize + ralign - 1);
  rvalue = GUM_ALIGN_POINTER (GumFFIValue *, rvalue, ralign);

  if (argc > 0)
  {
    gsize arglist_size, arglist_alignment, offset, i;

    avalue = g_newa (void *, MAX (nargs, argc));

    arglist_size = self->arglist_size;
    if (is_variadic && argc > nargs)
    {
      gsize type_idx;

      atypes = g_newa (ffi_type *, argc);

      memcpy (atypes, cif->arg_types, nargs * sizeof (void *));
      for (i = nargs, type_idx = nargs_fixed; i != argc; i++)
      {
        ffi_type * t = cif->arg_types[type_idx];

        atypes[i] = t;
        arglist_size = GUM_ALIGN_SIZE (arglist_size, t->alignment);
        arglist_size += t->size;

        if (++type_idx >= nargs)
          type_idx = nargs_fixed;
      }

      cif = &tmp_cif;
      if (ffi_prep_cif_var (cif, self->abi, (guint) nargs_fixed,
          (guint) argc, rtype, atypes) != FFI_OK)
      {
        return _gum_quick_throw_literal (ctx,
            "failed to compile function call interface");
      }
    }

    arglist_alignment = atypes[0]->alignment;
    avalues = g_alloca (arglist_size + arglist_alignment - 1);
    avalues = GUM_ALIGN_POINTER (guint8 *, avalues, arglist_alignment);

    /* Prefill with zero to clear high bits of values smaller than a pointer. */
    memset (avalues, 0, arglist_size);

    offset = 0;
    for (i = 0; i != argc; i++)
    {
      ffi_type * t;
      GumFFIValue * v;

      t = atypes[i];
      offset = GUM_ALIGN_SIZE (offset, t->alignment);
      v = (GumFFIValue *) (avalues + offset);

      if (!gum_quick_get_ffi_value (ctx, argv_index + i, t, core, v))
        return _gum_quick_throw_literal (ctx, "invalid argument value");
      avalue[i] = v;

      offset += t->size;
    }

    while (i < nargs)
      avalue[i++] = &tmp_value;
  }
  else
  {
    avalue = NULL;
  }

  scheduling = self->scheduling;
  exceptions = self->exceptions;
  traps = self->traps;
  return_shape = self->return_shape;
  system_error = -1;

  {
    GumQuickScope scope = GUM_QUICK_SCOPE_INIT (core);
    GumInterceptor * interceptor = core->interceptor->interceptor;
    GumStalker * stalker = NULL;

    if (exceptions == GUM_QUICK_EXCEPTIONS_PROPAGATE ||
        gum_exceptor_try (core->exceptor, &exceptor_scope))
    {
      if (exceptions == GUM_QUICK_EXCEPTIONS_STEAL)
        gum_interceptor_save (&invocation_state);

      if (scheduling == GUM_QUICK_SCHEDULING_COOPERATIVE)
      {
        _gum_quick_scope_suspend (&scope);

        gum_interceptor_unignore_current_thread (interceptor);
      }

      if (traps == GUM_QUICK_CODE_TRAPS_ALL)
      {
        _gum_quick_stalker_process_pending (core->stalker, scope.previous_scope);

        stalker = _gum_quick_stalker_get (core->stalker);
        gum_stalker_activate (stalker,
            GUM_FUNCPTR_TO_POINTER (implementation));
      }

      ffi_call (cif, implementation, rvalue, avalue);

      g_clear_pointer (&stalker, gum_stalker_deactivate);

      if (return_shape == GUM_QUICK_RETURN_DETAILED)
        system_error = gum_thread_get_system_error ();
    }

    g_clear_pointer (&stalker, gum_stalker_deactivate);

    if (scheduling == GUM_QUICK_SCHEDULING_COOPERATIVE)
    {
      gum_interceptor_ignore_current_thread (interceptor);

      _gum_quick_scope_resume (&scope);
    }
  }

  if (exceptions == GUM_QUICK_EXCEPTIONS_STEAL &&
      gum_exceptor_catch (core->exceptor, &exceptor_scope))
  {
    gum_interceptor_restore (&invocation_state);

    _gum_quick_throw_native (ctx, &exceptor_scope.exception, core);
  }

  if (return_shape == GUM_QUICK_RETURN_DETAILED)
  {
    quick_push_object (ctx);

    gum_quick_push_ffi_value (ctx, rvalue, rtype, core);
    quick_put_prop_string (ctx, -2, "value");

    quick_push_int (ctx, system_error);
    quick_put_prop_string (ctx, -2, GUMJS_SYSTEM_ERROR_FIELD);
  }
  else
  {
    gum_quick_push_ffi_value (ctx, rvalue, rtype, core);
  }
  return 1;
}

static int
gumjs_ffi_function_invoke (JSContext * ctx,
                           const GumQuickArgs * args)
{
  GumQuickFFIFunction * self;
  int argc;
  int argv_index;

  quick_push_this (ctx);
  self = _gum_quick_require_data (ctx, -1);

  argc = args->count;
  argv_index = 0;

  return gum_quick_ffi_function_invoke (self, ctx, self->implementation, argc,
      argv_index);
}

static int
gumjs_ffi_function_call (JSContext * ctx,
                         GumQuickHeapPtr type,
                         const GumQuickArgs * args)
{
  GumQuickHeapPtr receiver;
  GumQuickFFIFunction * func;
  GCallback impl;
  int argc;
  int argv_index;

  if (args->count == 0 || quick_is_null_or_undefined (ctx, 0))
  {
    receiver = NULL;
  }
  else if (quick_is_object (ctx, 0))
  {
    receiver = quick_require_heapptr (ctx, 0);
  }
  else
  {
    return _gum_quick_throw_literal (ctx, "invalid receiver");
  }

  gumjs_ffi_function_get (ctx, type, receiver, args->core, &func, &impl);

  argc = MAX ((int) args->count - 1, 0);
  argv_index = 1;

  return gum_quick_ffi_function_invoke (func, ctx, impl, argc, argv_index);
}

static int
gumjs_ffi_function_apply (JSContext * ctx,
                          GumQuickHeapPtr type,
                          const GumQuickArgs * args)
{
  GumQuickHeapPtr receiver;
  const int argv_array_index = 1;
  GumQuickFFIFunction * func;
  GCallback impl;
  int argc, i;
  int argv_index;

  if (quick_is_null_or_undefined (ctx, 0))
  {
    receiver = NULL;
  }
  else if (quick_is_object (ctx, 0))
  {
    receiver = quick_require_heapptr (ctx, 0);
  }
  else
  {
    return _gum_quick_throw_literal (ctx, "invalid receiver");
  }

  gumjs_ffi_function_get (ctx, type, receiver, args->core, &func, &impl);

  if (quick_is_null_or_undefined (ctx, argv_array_index))
  {
    argc = 0;
    argv_index = 1;
  }
  else
  {
    if (!quick_is_array (ctx, argv_array_index))
      return _gum_quick_throw_literal (ctx, "expected an array");

    argc = quick_get_length (ctx, argv_array_index);
    argv_index = quick_get_top_index (ctx) + 1;
    for (i = 0; i != argc; i++)
    {
      quick_get_prop_index (ctx, argv_array_index, (quick_uarridx_t) i);
    }
  }

  return gum_quick_ffi_function_invoke (func, ctx, impl, argc, argv_index);
}

static void
gumjs_ffi_function_get (JSContext * ctx,
                        GumQuickHeapPtr type,
                        GumQuickHeapPtr receiver,
                        GumQuickCore * core,
                        GumQuickFFIFunction ** func,
                        GCallback * implementation)
{
  GumQuickFFIFunction * f;

  quick_push_heapptr (ctx, type);
  quick_push_this (ctx);

  if (quick_instanceof (ctx, -1, -2))
  {
    f = _gum_quick_require_data (ctx, -1);

    *func = f;

    if (receiver != NULL)
    {
      quick_push_heapptr (ctx, receiver);
      *implementation = GUM_POINTER_TO_FUNCPTR (GCallback,
          _gum_quick_require_pointer (ctx, -1, core));

      quick_pop_3 (ctx);
    }
    else
    {
      *implementation = f->implementation;

      quick_pop_2 (ctx);
    }
  }
  else
  {
    if (receiver == NULL)
      return _gum_quick_throw_literal (ctx, "bad argument");
    quick_push_heapptr (ctx, receiver);
    if (!quick_instanceof (ctx, -1, -3))
      return _gum_quick_throw_literal (ctx, "bad argument");

    f = _gum_quick_require_data (ctx, -1);

    *func = f;
    *implementation = f->implementation;

    quick_pop_3 (ctx);
  }
}

static void
gum_quick_ffi_function_params_init (GumQuickFFIFunctionParams * params,
                                    JSCFunction * invoke,
                                    GumQuickHeapPtr prototype,
                                    GumQuickReturnValueShape return_shape,
                                    const GumQuickArgs * args)
{
  JSContext * ctx = args->ctx;
  GumQuickHeapPtr abi_or_options;

  params->invoke = invoke;
  params->prototype = prototype;

  abi_or_options = NULL;
  if (!_gum_quick_args_parse (args, "pVA|V", &params->implementation,
      &params->return_type, &params->argument_types, &abi_or_options))
  {
    return JS_EXCEPTION;
  }
  params->abi_name = NULL;
  params->scheduling = GUM_QUICK_SCHEDULING_COOPERATIVE;
  params->exceptions = GUM_QUICK_EXCEPTIONS_STEAL;
  params->traps = GUM_QUICK_CODE_TRAPS_DEFAULT;
  params->return_shape = return_shape;

  quick_push_heapptr (ctx, abi_or_options);
  if (quick_is_string (ctx, -1))
  {
    params->abi_name = quick_require_string (ctx, -1);
  }
  else if (quick_is_object (ctx, -1) && !quick_is_null (ctx, -1))
  {
    int options_index;

    options_index = quick_require_top_index (ctx);

    quick_get_prop_string (ctx, options_index, "abi");
    if (!quick_is_undefined (ctx, -1))
      params->abi_name = quick_require_string (ctx, -1);

    quick_get_prop_string (ctx, options_index, "scheduling");
    if (!quick_is_undefined (ctx, -1))
      params->scheduling = gum_quick_require_scheduling_behavior (ctx, -1);

    quick_get_prop_string (ctx, options_index, "exceptions");
    if (!quick_is_undefined (ctx, -1))
      params->exceptions = gum_quick_require_exceptions_behavior (ctx, -1);

    quick_get_prop_string (ctx, options_index, "traps");
    if (!quick_is_undefined (ctx, -1))
      params->traps = gum_quick_require_code_traps (ctx, -1);

    quick_pop_n (ctx, 4);
  }
  else if (!quick_is_undefined (ctx, -1))
  {
    return _gum_quick_throw_literal (ctx,
        "expected string or object containing options");
  }
  quick_pop (ctx);
}

static GumQuickSchedulingBehavior
gum_quick_require_scheduling_behavior (JSContext * ctx,
                                     int index)
{
  const gchar * value;

  value = quick_require_string (ctx, index);

  if (strcmp (value, "cooperative") == 0)
    return GUM_QUICK_SCHEDULING_COOPERATIVE;

  if (strcmp (value, "exclusive") != 0)
    return _gum_quick_throw_literal (ctx, "invalid scheduling behavior value");
  return GUM_QUICK_SCHEDULING_EXCLUSIVE;
}

static GumQuickExceptionsBehavior
gum_quick_require_exceptions_behavior (JSContext * ctx,
                                     int index)
{
  const gchar * value;

  value = quick_require_string (ctx, index);

  if (strcmp (value, "steal") == 0)
    return GUM_QUICK_EXCEPTIONS_STEAL;

  if (strcmp (value, "propagate") != 0)
    return _gum_quick_throw_literal (ctx, "invalid exceptions behavior value");
  return GUM_QUICK_EXCEPTIONS_PROPAGATE;
}

static GumQuickCodeTraps
gum_quick_require_code_traps (JSContext * ctx,
                            int index)
{
  const gchar * value;

  value = quick_require_string (ctx, index);

  if (strcmp (value, "all") == 0)
    return GUM_QUICK_CODE_TRAPS_ALL;

  if (strcmp (value, "default") != 0)
    return _gum_quick_throw_literal (ctx, "invalid code traps value");
  return GUM_QUICK_CODE_TRAPS_DEFAULT;
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_native_callback_construct)
{
  GumQuickHeapPtr func, rtype_value, atypes_array;
  gchar * abi_str = NULL;
  GumQuickCore * core = args->core;
  GumQuickNativeCallback * callback;
  GumQuickNativePointer * ptr;
  ffi_type * rtype;
  int nargs, i;
  ffi_abi abi;

  if (!quick_is_constructor_call (ctx))
  {
    return _gum_quick_throw_literal (ctx,
        "use `new NativeCallback()` to create a new instance");
  }

  if (!_gum_quick_args_parse (args, "FVA|s", &func, &rtype_value, &atypes_array,
      &abi_str))
  {
    return JS_EXCEPTION;
  }

  callback = g_slice_new0 (GumQuickNativeCallback);
  ptr = &callback->parent;
  callback->func = func;
  callback->core = core;

  if (!gum_quick_get_ffi_type (ctx, rtype_value, &rtype, &callback->data))
    goto invalid_return_type;

  quick_push_heapptr (ctx, atypes_array);

  nargs = quick_get_length (ctx, -1);

  callback->atypes = g_new (ffi_type *, nargs);

  for (i = 0; i != nargs; i++)
  {
    GumQuickHeapPtr atype_value;
    ffi_type ** atype;

    quick_get_prop_index (ctx, -1, (quick_uarridx_t) i);
    atype_value = quick_get_heapptr (ctx, -1);

    atype = &callback->atypes[i];

    if (!gum_quick_get_ffi_type (ctx, atype_value, atype, &callback->data))
      goto invalid_argument_type;

    quick_pop (ctx);
  }

  quick_pop (ctx);

  if (abi_str != NULL)
  {
    if (!gum_quick_get_ffi_abi (ctx, abi_str, &abi))
      goto invalid_abi;
  }
  else
  {
    abi = FFI_DEFAULT_ABI;
  }

  callback->closure = ffi_closure_alloc (sizeof (ffi_closure), &ptr->value);
  if (callback->closure == NULL)
    goto alloc_failed;

  if (ffi_prep_cif (&callback->cif, abi, (guint) nargs, rtype,
      callback->atypes) != FFI_OK)
    goto compilation_failed;

  if (ffi_prep_closure_loc (callback->closure, &callback->cif,
      gum_quick_native_callback_invoke, callback, ptr->value) != FFI_OK)
    goto prepare_failed;

  quick_push_this (ctx);

  _gum_quick_put_data (ctx, -1, callback);

  quick_push_heapptr (ctx, func);
  quick_put_prop_string (ctx, -2, QUICK_HIDDEN_SYMBOL ("func"));

  quick_pop (ctx);

  return JS_UNDEFINED;

invalid_return_type:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "invalid return type");
  }
invalid_argument_type:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "invalid argument type");
  }
invalid_abi:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "invalid abi");
  }
alloc_failed:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "failed to allocate closure");
  }
compilation_failed:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "failed to compile function call interface");
  }
prepare_failed:
  {
    gum_quick_native_callback_finalize (callback, FALSE);
    return _gum_quick_throw_literal (ctx, "failed to prepare closure");
  }

  g_assert_not_reached ();
  return JS_UNDEFINED;
}

GUMJS_DEFINE_FINALIZER (gumjs_native_callback_finalize)
{
  GumQuickNativeCallback * self;
  gboolean heap_destruct;

  self = _gum_quick_steal_data (ctx, 0);
  if (self == NULL)
    return JS_UNDEFINED;

  heap_destruct = quick_require_boolean (ctx, 1);
  gum_quick_native_callback_finalize (self, heap_destruct);

  return JS_UNDEFINED;
}

static void
gum_quick_native_callback_finalize (GumQuickNativeCallback * callback,
                                  gboolean heap_destruct)
{
  ffi_closure_free (callback->closure);

  while (callback->data != NULL)
  {
    GSList * head = callback->data;
    g_free (head->data);
    callback->data = g_slist_delete_link (callback->data, head);
  }
  g_free (callback->atypes);

  g_slice_free (GumQuickNativeCallback, callback);
}

static void
gum_quick_native_callback_invoke (ffi_cif * cif,
                                void * return_value,
                                void ** args,
                                void * user_data)
{
  GumQuickNativeCallback * self = user_data;
  GumQuickCore * core = self->core;
  GumQuickScope scope;
  JSContext * ctx = core->ctx;
  ffi_type * rtype = cif->rtype;
  GumFFIValue * retval = return_value;
  guint i;
  GumInvocationContext * ic;
  GumQuickInvocationContext * jic = NULL;
  gboolean success;

  _gum_quick_scope_enter (&scope, core);

  if (rtype != &ffi_type_void)
  {
    /*
     * Ensure:
     * - high bits of values smaller than a pointer are cleared to zero
     * - we return something predictable in case of a JS exception
     */
    retval->v_pointer = NULL;
  }

  quick_push_heapptr (ctx, self->func);

  ic = gum_interceptor_get_current_invocation ();
  if (ic != NULL)
  {
    jic = _gum_quick_interceptor_obtain_invocation_context (core->interceptor);
    _gum_quick_invocation_context_reset (jic, ic);
    quick_push_heapptr (ctx, jic->object);
  }
  else
  {
    quick_push_undefined (ctx);
  }

  for (i = 0; i != cif->nargs; i++)
    gum_quick_push_ffi_value (ctx, args[i], cif->arg_types[i], core);

  success = _gum_quick_scope_call_method (&scope, cif->nargs);

  if (jic != NULL)
  {
    _gum_quick_invocation_context_reset (jic, NULL);
    _gum_quick_interceptor_release_invocation_context (core->interceptor, jic);
  }

  if (success && cif->rtype != &ffi_type_void)
  {
    gum_quick_get_ffi_value (ctx, -1, cif->rtype, core, retval);
  }

  quick_pop (ctx);

  _gum_quick_scope_leave (&scope);
}

static GumQuickCpuContext *
gumjs_cpu_context_from_this (JSValueConst this_val,
                             GumQuickCore * core)
{
  return JS_GetOpaque (this_val, core->cpu_context_class_id);
}

GUMJS_DEFINE_FINALIZER (gumjs_cpu_context_finalize)
{
  GumQuickCpuContext * self;

  self = _gum_quick_steal_data (ctx, 0);
  if (self == NULL)
    return JS_UNDEFINED;

  g_slice_free (GumQuickCpuContext, self);

  return JS_UNDEFINED;
}

static JSValue
gumjs_cpu_context_set_register (GumQuickCpuContext * self,
                                JSContext * ctx,
                                JSValueConst value,
                                gpointer * reg)
{
  if (self->access == GUM_CPU_CONTEXT_READONLY)
    return _gum_quick_throw_literal (ctx, "invalid operation");

  return _gum_quick_native_pointer_parse (value, reg, self->core)
      ? JS_UNDEFINED
      : JS_EXCEPTION;
}

static GumSourceMap *
gumjs_source_map_from_this (const GumQuickArgs * args)
{
  JSContext * ctx = args->ctx;
  GumSourceMap * self;

  quick_push_this (ctx);
  self = _gum_quick_require_data (ctx, -1);
  quick_pop (ctx);

  return self;
}

static JSValue
gumjs_source_map_new (const gchar * json,
                      GumQuickCore * core)
{
  JSValue result;
  JSContext * ctx = core->ctx;
  JSValue json_val;

  json_val = JS_NewString (ctx, json);

  result = JS_CallConstructor (ctx, core->source_map_ctor, 1, &json_val);

  JS_FreeValue (ctx, json_val);

  return result;
}

GUMJS_DEFINE_CONSTRUCTOR (gumjs_source_map_construct)
{
  const gchar * json;
  GumSourceMap * self;

  if (!quick_is_constructor_call (ctx))
    _gum_quick_throw_literal (ctx, "use `new SourceMap()` to create a new instance");

  if (!_gum_quick_args_parse (args, "s", &json))
    return JS_EXCEPTION;

  self = gum_source_map_new (json);
  if (self == NULL)
    _gum_quick_throw_literal (ctx, "invalid source map");

  quick_push_this (ctx);
  _gum_quick_put_data (ctx, -1, self);
  quick_pop (ctx);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FINALIZER (gumjs_source_map_finalize)
{
  GumSourceMap * self;

  self = _gum_quick_steal_data (ctx, 0);
  if (self == NULL)
    return JS_UNDEFINED;

  g_object_unref (self);

  return JS_UNDEFINED;
}

GUMJS_DEFINE_FUNCTION (gumjs_source_map_resolve)
{
  GumSourceMap * self;
  guint line, column;
  const gchar * source, * name;

  self = gumjs_source_map_from_this (args);

  if (args->count == 1)
  {
    if (!_gum_quick_args_parse (args, "u", &line))
      return JS_EXCEPTION;
    column = G_MAXUINT;
  }
  else
  {
    if (!_gum_quick_args_parse (args, "uu", &line, &column))
      return JS_EXCEPTION;
  }

  if (gum_source_map_resolve (self, &line, &column, &source, &name))
  {
    quick_push_array (ctx);

    quick_push_string (ctx, source);
    quick_put_prop_index (ctx, -2, 0);

    quick_push_uint (ctx, line);
    quick_put_prop_index (ctx, -2, 1);

    quick_push_uint (ctx, column);
    quick_put_prop_index (ctx, -2, 2);

    if (name != NULL)
      quick_push_string (ctx, name);
    else
      quick_push_null (ctx);
    quick_put_prop_index (ctx, -2, 3);
  }
  else
  {
    quick_push_null (ctx);
  }

  return 1;
}

static GumQuickWeakRef *
gum_quick_weak_ref_new (guint id,
                        JSValue callback,
                        GumQuickCore * core)
{
  GumQuickWeakRef * ref;

  ref = g_slice_new (GumQuickWeakRef);
  ref->id = id;
  _gum_quick_protect (core->current_scope->ctx, callback);
  ref->callback = callback;
  ref->core = core;

  return ref;
}

static void
gum_quick_weak_ref_clear (GumQuickWeakRef * ref)
{
  GumQuickCore * core = ref->core;
  GumQuickScope scope = GUM_QUICK_SCOPE_INIT (core);
  JSContext * ctx = scope.ctx;

  quick_push_heapptr (ctx, ref->callback);
  _gum_quick_scope_call (&scope, 0);
  quick_pop (ctx);

  _gum_quick_unprotect (ctx, ref->callback);
  ref->callback = NULL;

  ref->core = NULL;
}

static JSValue
gum_quick_core_schedule_callback (GumQuickCore * self,
                                  const GumQuickArgs * args,
                                  gboolean repeat)
{
  GumQuickHeapPtr func;
  gsize delay;
  guint id;
  GSource * source;
  GumQuickScheduledCallback * callback;

  if (repeat)
  {
    if (!_gum_quick_args_parse (args, "FZ", &func, &delay))
      return JS_EXCEPTION;
  }
  else
  {
    delay = 0;
    if (!_gum_quick_args_parse (args, "F|Z", &func, &delay))
      return JS_EXCEPTION;
  }

  id = self->next_callback_id++;
  if (delay == 0)
    source = g_idle_source_new ();
  else
    source = g_timeout_source_new ((guint) delay);

  callback = gum_scheduled_callback_new (id, func, repeat, source, self);
  g_source_set_callback (source, (GSourceFunc) gum_scheduled_callback_invoke,
      callback, (GDestroyNotify) gum_scheduled_callback_free);

  g_hash_table_insert (self->scheduled_callbacks, GINT_TO_POINTER (id),
      callback);
  g_queue_push_tail (&self->current_scope->scheduled_sources, source);

  return JS_NewUint32 (self->ctx, id);
}

static GumQuickScheduledCallback *
gum_quick_core_try_steal_scheduled_callback (GumQuickCore * self,
                                           gint id)
{
  GumQuickScheduledCallback * callback;
  gpointer raw_id;

  raw_id = GINT_TO_POINTER (id);

  callback = g_hash_table_lookup (self->scheduled_callbacks, raw_id);
  if (callback == NULL)
    return NULL;

  g_hash_table_remove (self->scheduled_callbacks, raw_id);

  return callback;
}

static GumQuickScheduledCallback *
gum_scheduled_callback_new (guint id,
                            GumQuickHeapPtr func,
                            gboolean repeat,
                            GSource * source,
                            GumQuickCore * core)
{
  GumQuickScheduledCallback * callback;

  callback = g_slice_new (GumQuickScheduledCallback);
  callback->id = id;
  _gum_quick_protect (core->current_scope->ctx, func);
  callback->func = func;
  callback->repeat = repeat;
  callback->source = source;
  callback->core = core;

  return callback;
}

static void
gum_scheduled_callback_free (GumQuickScheduledCallback * callback)
{
  GumQuickCore * core = callback->core;
  GumQuickScope scope;
  JSContext * ctx;

  ctx = _gum_quick_scope_enter (&scope, core);
  _gum_quick_core_unpin (core);
  _gum_quick_unprotect (ctx, callback->func);
  _gum_quick_scope_leave (&scope);

  g_slice_free (GumQuickScheduledCallback, callback);
}

static gboolean
gum_scheduled_callback_invoke (GumQuickScheduledCallback * self)
{
  GumQuickCore * core = self->core;
  JSContext * ctx;
  GumQuickScope scope;

  ctx = _gum_quick_scope_enter (&scope, self->core);

  quick_push_heapptr (ctx, self->func);
  _gum_quick_scope_call (&scope, 0);
  quick_pop (ctx);

  if (!self->repeat)
  {
    if (gum_quick_core_try_steal_scheduled_callback (core, self->id) != NULL)
      _gum_quick_core_pin (core);
  }

  _gum_quick_scope_leave (&scope);

  return self->repeat;
}

static GumQuickExceptionSink *
gum_quick_exception_sink_new (GumQuickHeapPtr callback,
                            GumQuickCore * core)
{
  GumQuickExceptionSink * sink;

  sink = g_slice_new (GumQuickExceptionSink);
  _gum_quick_protect (core->current_scope->ctx, callback);
  sink->callback = callback;
  sink->core = core;

  return sink;
}

static void
gum_quick_exception_sink_free (GumQuickExceptionSink * sink)
{
  _gum_quick_unprotect (sink->core->current_scope->ctx, sink->callback);
  g_slice_free (GumQuickExceptionSink, sink);
}

static void
gum_quick_exception_sink_handle_exception (GumQuickExceptionSink * self,
                                           JSValueConst exception)
{
  GumQuickCore * core = self->core;
  JSContext * ctx = core->current_scope->ctx;
  GumQuickHeapPtr callback = self->callback;

  quick_push_heapptr (ctx, callback);
  quick_dup (ctx, -2);
  quick_pcall (ctx, 1);
  quick_pop (ctx);
}

static GumQuickMessageSink *
gum_quick_message_sink_new (GumQuickHeapPtr callback,
                          GumQuickCore * core)
{
  GumQuickMessageSink * sink;

  sink = g_slice_new (GumQuickMessageSink);
  _gum_quick_protect (core->current_scope->ctx, callback);
  sink->callback = callback;
  sink->core = core;

  return sink;
}

static void
gum_quick_message_sink_free (GumQuickMessageSink * sink)
{
  _gum_quick_unprotect (sink->core->current_scope->ctx, sink->callback);
  g_slice_free (GumQuickMessageSink, sink);
}

GUMJS_DEFINE_FINALIZER (gum_quick_message_data_finalize)
{
  gpointer data;

  data = quick_require_buffer_data (ctx, 0, NULL);
  g_free (data);

  return JS_UNDEFINED;
}

static void
gum_quick_message_sink_post (GumQuickMessageSink * self,
                           const gchar * message,
                           GBytes * data,
                           GumQuickScope * scope)
{
  JSContext * ctx = self->core->current_scope->ctx;

  quick_push_heapptr (ctx, self->callback);

  quick_push_string (ctx, message);
  if (data != NULL)
  {
    gpointer data_buffer;
    gsize data_size;

    data_buffer = g_bytes_unref_to_data (data, &data_size);

    quick_push_external_buffer (ctx);
    quick_config_buffer (ctx, -1, data_buffer, data_size);

    quick_push_buffer_object (ctx, -1, 0, data_size, QUICK_BUFOBJ_ARRAYBUFFER);

    quick_swap (ctx, -2, -1);
    quick_pop (ctx);

    quick_push_c_function (ctx, gum_quick_message_data_finalize, 1);
    quick_set_finalizer (ctx, -2);
  }
  else
  {
    quick_push_null (ctx);
  }

  _gum_quick_scope_call (scope, 2);

  quick_pop (ctx);
}

static gboolean
gum_quick_get_ffi_type (JSContext * ctx,
                      GumQuickHeapPtr value,
                      ffi_type ** type,
                      GSList ** data)
{
  gboolean success = FALSE;
  int i;

  quick_push_heapptr (ctx, value);

  if (quick_is_string (ctx, -1))
  {
    const gchar * type_name = quick_require_string (ctx, -1);

    success = gum_ffi_try_get_type_by_name (type_name, type);
  }
  else if (quick_is_array (ctx, -1))
  {
    int length;
    ffi_type ** fields, * struct_type;

    length = quick_get_length (ctx, -1);

    fields = g_new (ffi_type *, length + 1);
    *data = g_slist_prepend (*data, fields);

    for (i = 0; i != length; i++)
    {
      GumQuickHeapPtr field_value;

      quick_get_prop_index (ctx, -1, (quick_uarridx_t) i);
      field_value = quick_get_heapptr (ctx, -1);
      quick_pop (ctx);

      if (!gum_quick_get_ffi_type (ctx, field_value, &fields[i], data))
        goto beach;
    }

    fields[length] = NULL;

    struct_type = g_new0 (ffi_type, 1);
    struct_type->type = FFI_TYPE_STRUCT;
    struct_type->elements = fields;
    *data = g_slist_prepend (*data, struct_type);

    *type = struct_type;
    success = TRUE;
  }

beach:
  quick_pop (ctx);

  return success;
}

static gboolean
gum_quick_get_ffi_abi (JSContext * ctx,
                     const gchar * name,
                     ffi_abi * abi)
{
  if (gum_ffi_try_get_abi_by_name (name, abi))
    return TRUE;

  _gum_quick_throw_literal (ctx, "invalid abi specified");
  return FALSE;
}

static gboolean
gum_quick_get_ffi_value (JSContext * ctx,
                       int index,
                       const ffi_type * type,
                       GumQuickCore * core,
                       GumFFIValue * value)
{
  guint u;
  gint64 i64;
  guint64 u64;

  if (type == &ffi_type_void)
  {
    value->v_pointer = NULL;
  }
  else if (type == &ffi_type_pointer)
  {
    if (!_gum_quick_get_pointer (ctx, index, core, &value->v_pointer))
      return FALSE;
  }
  else if (type == &ffi_type_sint8)
  {
    if (!quick_is_number (ctx, index))
      return FALSE;
    value->v_sint8 = quick_require_int (ctx, index);
  }
  else if (type == &ffi_type_uint8)
  {
    if (_gum_quick_get_uint (ctx, index, &u))
      value->v_uint8 = u;
    else
      return FALSE;
  }
  else if (type == &ffi_type_sint16)
  {
    if (!quick_is_number (ctx, index))
      return FALSE;
    value->v_sint16 = quick_require_int (ctx, index);
  }
  else if (type == &ffi_type_uint16)
  {
    if (_gum_quick_get_uint (ctx, index, &u))
      value->v_uint16 = u;
    else
      return FALSE;
  }
  else if (type == &ffi_type_sint32)
  {
    if (!quick_is_number (ctx, index))
      return FALSE;
    value->v_sint32 = quick_require_int (ctx, index);
  }
  else if (type == &ffi_type_uint32)
  {
    if (_gum_quick_get_uint (ctx, index, &u))
      value->v_uint32 = u;
    else
      return FALSE;
  }
  else if (type == &ffi_type_sint64)
  {
    if (!_gum_quick_get_int64 (ctx, index, core, &i64))
      return FALSE;
    value->v_sint64 = i64;
  }
  else if (type == &ffi_type_uint64)
  {
    if (!_gum_quick_get_uint64 (ctx, index, core, &u64))
      return FALSE;
    value->v_uint64 = u64;
  }
  else if (type == &ffi_type_float)
  {
    if (!quick_is_number (ctx, index))
      return FALSE;
    value->v_float = quick_require_number (ctx, index);
  }
  else if (type == &ffi_type_double)
  {
    if (!quick_is_number (ctx, index))
      return FALSE;
    value->v_double = quick_require_number (ctx, index);
  }
  else if (type->type == FFI_TYPE_STRUCT)
  {
    ffi_type ** const field_types = type->elements, ** t;
    int length, expected_length, i;
    guint8 * field_values;
    gsize offset;

    if (!quick_is_array (ctx, index))
      return FALSE;

    length = quick_get_length (ctx, index);

    expected_length = 0;
    for (t = field_types; *t != NULL; t++)
      expected_length++;

    if (length != expected_length)
      return FALSE;

    field_values = (guint8 *) value;
    offset = 0;

    for (i = 0; i != length; i++)
    {
      const ffi_type * field_type = field_types[i];
      GumFFIValue * field_value;
      gboolean valid;

      offset = GUM_ALIGN_SIZE (offset, field_type->alignment);

      field_value = (GumFFIValue *) (field_values + offset);
      quick_get_prop_index (ctx, index, (quick_uarridx_t) i);
      valid = gum_quick_get_ffi_value (ctx, -1, field_type, core, field_value);
      quick_pop (ctx);

      if (!valid)
        return FALSE;

      offset += field_type->size;
    }
  }
  else
  {
    g_assert_not_reached ();
  }

  return TRUE;
}

static void
gum_quick_push_ffi_value (JSContext * ctx,
                        const GumFFIValue * value,
                        const ffi_type * type,
                        GumQuickCore * core)
{
  if (type == &ffi_type_void)
  {
    quick_push_undefined (ctx);
  }
  else if (type == &ffi_type_pointer)
  {
    _gum_quick_push_native_pointer (ctx, value->v_pointer, core);
  }
  else if (type == &ffi_type_sint8)
  {
    quick_push_int (ctx, value->v_sint8);
  }
  else if (type == &ffi_type_uint8)
  {
    quick_push_uint (ctx, value->v_uint8);
  }
  else if (type == &ffi_type_sint16)
  {
    quick_push_int (ctx, value->v_sint16);
  }
  else if (type == &ffi_type_uint16)
  {
    quick_push_uint (ctx, value->v_uint16);
  }
  else if (type == &ffi_type_sint32)
  {
    quick_push_int (ctx, value->v_sint32);
  }
  else if (type == &ffi_type_uint32)
  {
    quick_push_uint (ctx, value->v_uint32);
  }
  else if (type == &ffi_type_sint64)
  {
    _gum_quick_push_int64 (ctx, value->v_sint64, core);
  }
  else if (type == &ffi_type_uint64)
  {
    _gum_quick_push_uint64 (ctx, value->v_uint64, core);
  }
  else if (type == &ffi_type_float)
  {
    quick_push_number (ctx, value->v_float);
  }
  else if (type == &ffi_type_double)
  {
    quick_push_number (ctx, value->v_double);
  }
  else if (type->type == FFI_TYPE_STRUCT)
  {
    ffi_type ** const field_types = type->elements, ** t;
    guint length, i;
    const guint8 * field_values;
    gsize offset;

    length = 0;
    for (t = field_types; *t != NULL; t++)
      length++;

    field_values = (const guint8 *) value;
    offset = 0;

    quick_push_array (ctx);

    for (i = 0; i != length; i++)
    {
      const ffi_type * field_type = field_types[i];
      const GumFFIValue * field_value;

      offset = GUM_ALIGN_SIZE (offset, field_type->alignment);
      field_value = (const GumFFIValue *) (field_values + offset);

      gum_quick_push_ffi_value (ctx, field_value, field_type, core);
      quick_put_prop_index (ctx, -2, i);

      offset += field_type->size;
    }
  }
  else
  {
    g_assert_not_reached ();
  }
}
