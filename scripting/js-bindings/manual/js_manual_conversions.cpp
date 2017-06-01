
#include "ScriptingCore.h"
#include "js_bindings_config.h"
#include "js_manual_conversions.h"
//#include "math/TransformUtils.h"
#include <cmath>
#include <sstream>
USING_NS_CC;

// JSStringWrapper
JSStringWrapper::JSStringWrapper()
: _buffer(nullptr)
{
}

JSStringWrapper::JSStringWrapper(JSString* str, JSContext* cx/* = NULL*/)
: _buffer(nullptr)
{
    set(str, cx);
}

JSStringWrapper::JSStringWrapper(jsval val, JSContext* cx/* = NULL*/)
: _buffer(nullptr)
{
    set(val, cx);
}

JSStringWrapper::~JSStringWrapper()
{
    JS_free(ScriptingCore::getInstance()->getGlobalContext(), (void*)_buffer);
}

void JSStringWrapper::set(jsval val, JSContext* cx)
{
    if (val.isString())
    {
        this->set(val.toString(), cx);
    }
    else
    {
        CC_SAFE_DELETE_ARRAY(_buffer);
    }
}

void JSStringWrapper::set(JSString* str, JSContext* cx)
{
    CC_SAFE_DELETE_ARRAY(_buffer);
    
    if (!cx)
    {
        cx = ScriptingCore::getInstance()->getGlobalContext();
    }
    // JS_EncodeString isn't supported in SpiderMonkey ff19.0.
    //buffer = JS_EncodeString(cx, string);

    //JS_GetStringCharsZ is removed in SpiderMonkey 33
//    unsigned short* pStrUTF16 = (unsigned short*)JS_GetStringCharsZ(cx, str);

//    _buffer = cc_utf16_to_utf8(pStrUTF16, -1, NULL, NULL);

    _buffer = JS_EncodeStringToUTF8(cx, JS::RootedString(cx, str));
}

const char* JSStringWrapper::get()
{
    return _buffer ? _buffer : "";
}



// JSFunctionWrapper
JSFunctionWrapper::JSFunctionWrapper(JSContext* cx, JSObject *jsthis, jsval fval)
: _cx(cx)
, _jsthis(jsthis)
, _fval(fval)
{
    JS::AddNamedValueRoot(cx, &this->_fval, "JSFunctionWrapper");
    JS::AddNamedObjectRoot(cx, &this->_jsthis, "JSFunctionWrapper");
}

JSFunctionWrapper::~JSFunctionWrapper()
{
    JS::RemoveValueRoot(this->_cx, &this->_fval);
    JS::RemoveObjectRoot(this->_cx, &this->_jsthis);
}

bool JSFunctionWrapper::invoke(unsigned int argc, jsval *argv, JS::MutableHandleValue rval)
{
    JSB_AUTOCOMPARTMENT_WITH_GLOBAL_OBJCET
    
    return JS_CallFunctionValue(this->_cx, JS::RootedObject(_cx, this->_jsthis.get()), JS::RootedValue(_cx, this->_fval.get()), JS::HandleValueArray::fromMarkedLocation(argc, argv), rval);
}

static DSize getSizeFromJSObject(JSContext *cx, JS::HandleObject sizeObject)
{
    JS::RootedValue jsr(cx);
    DSize out;
    JS_GetProperty(cx, sizeObject, "width", &jsr);
    double width = 0.0;
    JS::ToNumber(cx, jsr, &width);
    
    JS_GetProperty(cx, sizeObject, "height", &jsr);
    double height = 0.0;
    JS::ToNumber(cx, jsr, &height);
    
    
    // the out
    out.width  = width;
    out.height = height;
    
    return out;
}

bool jsval_to_opaque( JSContext *cx, JS::HandleValue vp, void **r)
{
#ifdef __LP64__

    // begin
    JS::RootedObject tmp_arg(cx);
    bool ok = JS_ValueToObject( cx, vp, &tmp_arg );
    JSB_PRECONDITION2( ok, cx, false, "Error converting value to object");
    JSB_PRECONDITION2( tmp_arg && JS_IsTypedArrayObject( tmp_arg ), cx, false, "Not a TypedArray object");
    JSB_PRECONDITION2( JS_GetTypedArrayByteLength( tmp_arg ) == sizeof(void*), cx, false, "Invalid Typed Array length");

    uint32_t* arg_array = (uint32_t*)JS_GetArrayBufferViewData( tmp_arg );
    uint64_t ret =  arg_array[0];
    ret = ret << 32;
    ret |= arg_array[1];

#else
    CCAssert( sizeof(int)==4, "");
    int32_t ret;
    if( ! jsval_to_int32(cx, vp, &ret ) )
      return false;
#endif
    *r = (void*)ret;
    return true;
}

bool jsval_to_int( JSContext *cx, JS::HandleValue vp, int *ret )
{
    // Since this is called to cast uint64 to uint32,
    // it is needed to initialize the value to 0 first
#ifdef __LP64__
    // When int size is 8 Bit (same as long), the following operation is needed
    if (sizeof(int) == 8)
    {
        long *tmp = (long*)ret;
        *tmp = 0;
    }
#endif
    return jsval_to_int32(cx, vp, (int32_t*)ret);
}

jsval opaque_to_jsval( JSContext *cx, void *opaque )
{
#ifdef __LP64__
    uint64_t number = (uint64_t)opaque;
    JSObject *typedArray = JS_NewUint32Array( cx, 2 );
    uint32_t *buffer = (uint32_t*)JS_GetArrayBufferViewData(typedArray);
    buffer[0] = number >> 32;
    buffer[1] = number & 0xffffffff;
    return OBJECT_TO_JSVAL(typedArray);
#else
    CCAssert(sizeof(int)==4, "");
    int32_t number = (int32_t) opaque;
    return INT_TO_JSVAL(number);
#endif
}

jsval c_class_to_jsval( JSContext *cx, void* handle, JS::HandleObject object, JSClass *klass, const char* class_name)
{
    JS::RootedObject jsobj(cx);
    
    jsobj = jsb_get_jsobject_for_proxy(handle);
    if( !jsobj ) {
        JS::RootedObject parent(cx);
        jsobj = JS_NewObject(cx, klass, object, parent);
        CCAssert(jsobj, "Invalid object");
        jsb_set_c_proxy_for_jsobject(jsobj, handle, JSB_C_FLAG_DO_NOT_CALL_FREE);
        jsb_set_jsobject_for_proxy(jsobj, handle);
    }
    
    return OBJECT_TO_JSVAL(jsobj);
}

bool jsval_to_c_class( JSContext *cx, JS::HandleValue vp, void **out_native, struct jsb_c_proxy_s **out_proxy)
{
    JS::RootedObject jsobj(cx);
    bool ok = JS_ValueToObject( cx, vp, &jsobj );
    JSB_PRECONDITION2(ok, cx, false, "Error converting jsval to object");
    
    struct jsb_c_proxy_s *proxy = jsb_get_c_proxy_for_jsobject(jsobj);
    *out_native = proxy->handle;
    if( out_proxy )
        *out_proxy = proxy;
    return true;
}

bool jsval_to_uint( JSContext *cx, JS::HandleValue vp, unsigned int *ret )
{
    // Since this is called to cast uint64 to uint32,
    // it is needed to initialize the value to 0 first
#ifdef __LP64__
    // When unsigned int size is 8 Bit (same as long), the following operation is needed
    if (sizeof(unsigned int)==8)
    {
        long *tmp = (long*)ret;
        *tmp = 0;
    }
#endif
    return jsval_to_int32(cx, vp, (int32_t*)ret);
}

jsval long_to_jsval( JSContext *cx, long number )
{
#ifdef __LP64__
    assert( sizeof(long)==8);

    char chr[128];
    snprintf(chr, sizeof(chr)-1, "%ld", number);
    JSString *ret_obj = JS_NewStringCopyZ(cx, chr);
    return STRING_TO_JSVAL(ret_obj);
#else
    CCAssert( sizeof(int)==4, "Error!");
    return INT_TO_JSVAL(number);
#endif
}

jsval ulong_to_jsval( JSContext *cx, unsigned long number )
{
#ifdef __LP64__
    assert( sizeof(unsigned long)==8);
    
    char chr[128];
    snprintf(chr, sizeof(chr)-1, "%lu", number);
    JSString *ret_obj = JS_NewStringCopyZ(cx, chr);
    return STRING_TO_JSVAL(ret_obj);
#else
    CCAssert( sizeof(int)==4, "Error!");
    return UINT_TO_JSVAL(number);
#endif
}

jsval long_long_to_jsval( JSContext *cx, long long number )
{
#if JSB_REPRESENT_LONGLONG_AS_STR
    char chr[128];
    snprintf(chr, sizeof(chr)-1, "%lld", number);
    JSString *ret_obj = JS_NewStringCopyZ(cx, chr);
    return STRING_TO_JSVAL(ret_obj);
    
#else
    CCASSERT( sizeof(long long)==8, "Error!");
    JSObject *typedArray = JS_NewUint32Array( cx, 2 );
    uint32_t *buffer = (uint32_t*)JS_GetArrayBufferViewData(typedArray, cx);
    buffer[0] = number >> 32;
    buffer[1] = number & 0xffffffff;
    return OBJECT_TO_JSVAL(typedArray);
#endif
}

bool jsval_to_charptr( JSContext *cx, JS::HandleValue vp, const char **ret )
{
    JSString *jsstr = JS::ToString( cx, vp );
    JSB_PRECONDITION2( jsstr, cx, false, "invalid string" );

    //XXX: what's this?
    // root it
//    vp = STRING_TO_JSVAL(jsstr);

    JSStringWrapper strWrapper(jsstr);
    
    // XXX: It is converted to String and then back to char* to autorelease the created object.
    const char *tmp = strWrapper.get();

    JSB_PRECONDITION2( tmp, cx, false, "Error creating string from UTF8");

    *ret = tmp;

    return true;
}

jsval charptr_to_jsval( JSContext *cx, const char *str)
{
    return c_string_to_jsval(cx, str);
}

bool JSB_jsval_typedarray_to_dataptr( JSContext *cx, JS::HandleValue vp, GLsizei *count, void **data, js::Scalar::Type t)
{
    JS::RootedObject jsobj(cx);
    bool ok = JS_ValueToObject( cx, vp, &jsobj );
    JSB_PRECONDITION2( ok && jsobj, cx, false, "Error converting value to object");
    
    // WebGL supports TypedArray and sequences for some of its APIs. So when converting a TypedArray, we should
    // also check for a possible non-Typed Array JS object, like a JS Array.
    
    if( JS_IsTypedArrayObject( jsobj ) ) {
        
        *count = JS_GetTypedArrayLength(jsobj);
        js::Scalar::Type type = JS_GetArrayBufferViewType(jsobj);
        JSB_PRECONDITION2(t==type, cx, false, "TypedArray type different than expected type");
        
        switch (t) {
            case js::Scalar::Int8:
            case js::Scalar::Uint8:
                *data = JS_GetUint8ArrayData(jsobj);
                break;
                
            case js::Scalar::Int16:
            case js::Scalar::Uint16:
                *data = JS_GetUint16ArrayData(jsobj);
                break;
                
            case js::Scalar::Int32:
            case js::Scalar::Uint32:
                *data = JS_GetUint32ArrayData(jsobj);
                break;
                
            case js::Scalar::Float32:
                *data = JS_GetFloat32ArrayData(jsobj);
                break;
                
            default:
                JSB_PRECONDITION2(false, cx, false, "Unsupported typedarray type");
                break;
        }
    } else if( JS_IsArrayObject(cx, jsobj)) {
        // Slow... avoid it. Use TypedArray instead, but the spec says that it can receive
        // Sequence<> as well.
        uint32_t length;
        JS_GetArrayLength(cx, jsobj, &length);
        
        for( uint32_t i=0; i<length;i++ ) {
            
            JS::RootedValue valarg(cx);
            JS_GetElement(cx, jsobj, i, &valarg);
            
            switch(t) {
                case js::Scalar::Int32:
                case js::Scalar::Uint32:
                {
                    uint32_t e = valarg.toInt32();
                    ((uint32_t*)data)[i] = e;
                    break;
                }
                case js::Scalar::Float32:
                {
                    double e = valarg.toNumber();
                    ((GLfloat*)data)[i] = (GLfloat)e;
                    break;
                }
                default:
                    JSB_PRECONDITION2(false, cx, false, "Unsupported typedarray type");
                    break;
            }
        }
        
    } else
        JSB_PRECONDITION2(false, cx, false, "Object shall be a TypedArray or Sequence");
    
    return true;
}

bool JSB_get_arraybufferview_dataptr( JSContext *cx, JS::HandleValue vp, GLsizei *count, GLvoid **data )
{
    JS::RootedObject jsobj(cx);
    bool ok = JS_ValueToObject( cx, vp, &jsobj );
    JSB_PRECONDITION2( ok && jsobj, cx, false, "Error converting value to object");
    JSB_PRECONDITION2( JS_IsArrayBufferViewObject(jsobj), cx, false, "Not an ArrayBufferView object");
    
    *data = JS_GetArrayBufferViewData(jsobj);
    *count = JS_GetArrayBufferViewByteLength(jsobj);
    
    return true;
}

//
//#pragma mark - Conversion Routines
bool jsval_to_ushort( JSContext *cx, JS::HandleValue vp, unsigned short *outval )
{
    bool ok = true;
    double dp;
    ok &= JS::ToNumber(cx, vp, &dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ok &= !isnan((float)dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");

    *outval = (unsigned short)dp;

    return ok;
}

bool jsval_to_int32( JSContext *cx, JS::HandleValue vp, int32_t *outval )
{
    bool ok = true;
    double dp;
    ok &= JS::ToNumber(cx, vp, &dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ok &= !isnan((float)dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    *outval = (int32_t)dp;
    
    return ok;
}

bool jsval_to_uint32( JSContext *cx, JS::HandleValue vp, uint32_t *outval )
{
    bool ok = true;
    double dp;
    ok &= JS::ToNumber(cx, vp, &dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ok &= !isnan((float)dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    *outval = (uint32_t)dp;
    
    return ok;
}

bool jsval_to_uint16( JSContext *cx, JS::HandleValue vp, uint16_t *outval )
{
    bool ok = true;
    double dp;
    ok &= JS::ToNumber(cx, vp, &dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ok &= !isnan((float)dp);
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    *outval = (uint16_t)dp;
    
    return ok;
}

// XXX: sizeof(long) == 8 in 64 bits on OS X... apparently on Windows it is 32 bits (???)
bool jsval_to_long( JSContext *cx, JS::HandleValue vp, long *r )
{
#ifdef __LP64__
    // compatibility check
    assert( sizeof(long)==8);
    JSString *jsstr = JS::ToString(cx, vp);
    JSB_PRECONDITION2(jsstr, cx, false, "Error converting value to string");
    
    char *str = JS_EncodeString(cx, jsstr);
    JSB_PRECONDITION2(str, cx, false, "Error encoding string");
    
    char *endptr;
    long ret = strtol(str, &endptr, 10);
    
    *r = ret;
    return true;
    
#else
    // compatibility check
    CCAssert( sizeof(int)==4, "");
    long ret = vp.toInt32();
#endif
    
    *r = ret;
    return true;
}


bool jsval_to_ulong( JSContext *cx, JS::HandleValue vp, unsigned long *out)
{
    if (out == nullptr)
        return false;
    
    long rval = 0;
    bool ret = false;
    ret = jsval_to_long(cx, vp, &rval);
    if (ret)
    {
        *out = (unsigned long)rval;
    }
    return ret;
}

bool jsval_to_long_long(JSContext *cx, JS::HandleValue vp, long long* r)
{
    JSString *jsstr = JS::ToString(cx, vp);
    JSB_PRECONDITION2(jsstr, cx, false, "Error converting value to string");
    
    char *str = JS_EncodeString(cx, jsstr);
    JSB_PRECONDITION2(str, cx, false, "Error encoding string");
    
    char *endptr;
#if(CC_TARGET_PLATFORM == CC_PLATFORM_WIN32 || CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    __int64 ret = _strtoi64(str, &endptr, 10);
#else
    long long ret = strtoll(str, &endptr, 10);
#endif
    
    *r = ret;
    return true;
}

bool jsval_to_std_string(JSContext *cx, JS::HandleValue v, std::string* ret) {
    if(v.isString() || v.isNumber())
    {
        JSString *tmp = JS::ToString(cx, v);
        JSB_PRECONDITION3(tmp, cx, false, "Error processing arguments");

        JSStringWrapper str(tmp);
        *ret = str.get();
        return true;
    }

    return false;
}

bool javal_to_viodpointe(JSContext *cx, JS::HandleValue v, void* context)
{
    JSObject *tmpObj = v.toObjectOrNull();
    js_proxy_t *jsProxy;
    if (tmpObj) {
        jsProxy = jsb_get_js_proxy(tmpObj);
        context = (void *)(jsProxy ? jsProxy->ptr : NULL);
    } else {
        context = nullptr;
    }
    return true;
}
bool jsval_to_dpoint(JSContext *cx, JS::HandleValue v, DPoint* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsx(cx);
    JS::RootedValue jsy(cx);
    double x, y;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "x", &jsx) &&
    JS_GetProperty(cx, tmp, "y", &jsy) &&
    JS::ToNumber(cx, jsx, &x) &&
    JS::ToNumber(cx, jsy, &y);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->x = (float)x;
    ret->y = (float)y;
    return true;
}
bool jsval_to_dpoint3d(JSContext *cx, JS::HandleValue v, CrossApp::DPoint3D* ret)
{
    JS::RootedObject tmp(cx);
    JS::RootedValue jsx(cx);
    JS::RootedValue jsy(cx);
    JS::RootedValue jsz(cx);
    double x, y ,z;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "x", &jsx) &&
    JS_GetProperty(cx, tmp, "y", &jsy) &&
    JS_GetProperty(cx, tmp, "z", &jsz) &&
    JS::ToNumber(cx, jsx, &x) &&
    JS::ToNumber(cx, jsy, &y) &&
    JS::ToNumber(cx, jsz, &z);
    
    if (!ok) {
        return false;
    }
    CCLog("x:%lf,y:%lf,z:%lf",x,y,z);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->x = (float)x;
    ret->y = (float)y;
    ret->z = (float)z;
    return true;
}

bool jsval_to_ccacceleration(JSContext* cx, JS::HandleValue v, CAAcceleration* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsx(cx);
    JS::RootedValue jsy(cx);
    JS::RootedValue jsz(cx);
    JS::RootedValue jstimestamp(cx);
    
    double x, y, timestamp, z;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "x", &jsx) &&
    JS_GetProperty(cx, tmp, "y", &jsy) &&
    JS_GetProperty(cx, tmp, "z", &jsz) &&
    JS_GetProperty(cx, tmp, "timestamp", &jstimestamp) &&
    JS::ToNumber(cx, jsx, &x) &&
    JS::ToNumber(cx, jsy, &y) &&
    JS::ToNumber(cx, jsz, &z) &&
    JS::ToNumber(cx, jstimestamp, &timestamp);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->x = x;
    ret->y = y;
    ret->z = z;
    ret->timestamp = timestamp;
    return true;
}

bool jsval_to_quaternion( JSContext *cx, JS::HandleValue v, CrossApp::Quaternion* ret)
{
    JS::RootedObject tmp(cx);
    JS::RootedValue x(cx);
    JS::RootedValue y(cx);
    JS::RootedValue z(cx);
    JS::RootedValue w(cx);
    
    double xx, yy, zz, ww;
    bool ok = v.isObject() &&
        JS_ValueToObject(cx, v, &tmp) &&
        JS_GetProperty(cx, tmp, "x", &x) &&
        JS_GetProperty(cx, tmp, "y", &y) &&
        JS_GetProperty(cx, tmp, "z", &z) &&
        JS_GetProperty(cx, tmp, "w", &w) &&
        JS::ToNumber(cx, x, &xx) &&
        JS::ToNumber(cx, y, &yy) &&
        JS::ToNumber(cx, z, &zz) &&
        JS::ToNumber(cx, w, &ww) &&
        !isnan((float)xx) && !isnan((float)yy) && !isnan((float)zz) && !isnan((float)ww);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");

    ret->set(xx, yy, zz, ww);

    return true;
}

bool jsval_to_drect(JSContext *cx, JS::HandleValue v, DRect* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsx(cx);
    JS::RootedValue jsy(cx);
    JS::RootedValue jswidth(cx);
    JS::RootedValue jsheight(cx);
    
    double x, y, width, height;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "x", &jsx) &&
    JS_GetProperty(cx, tmp, "y", &jsy) &&
    JS_GetProperty(cx, tmp, "width", &jswidth) &&
    JS_GetProperty(cx, tmp, "height", &jsheight) &&
    JS::ToNumber(cx, jsx, &x) &&
    JS::ToNumber(cx, jsy, &y) &&
    JS::ToNumber(cx, jswidth, &width) &&
    JS::ToNumber(cx, jsheight, &height);

    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->origin.x = x;
    ret->origin.y = y;
    ret->size.width = width;
    ret->size.height = height;
    return true;
}

bool jsval_to_dsize(JSContext *cx, JS::HandleValue v, DSize* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsw(cx);
    JS::RootedValue jsh(cx);
    double w, h;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "width", &jsw) &&
    JS_GetProperty(cx, tmp, "height", &jsh) &&
    JS::ToNumber(cx, jsw, &w) &&
    JS::ToNumber(cx, jsh, &h);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ret->width = w;
    ret->height = h;
    return true;
}
bool jsval_to_dhorizontallayout(JSContext *cx, JS::HandleValue v, CrossApp::DHorizontalLayout* horizontal){
    JS::RootedObject tmp(cx);
    JS::RootedValue jsleft(cx);
    JS::RootedValue jright(cx);
    JS::RootedValue jswidth(cx);
    JS::RootedValue jrcenter(cx);
    JS::RootedValue jrtype(cx);
    double left,right,width,center;
    int type = 0;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "left", &jsleft) &&
    JS_GetProperty(cx, tmp, "right", &jright) &&
    JS_GetProperty(cx, tmp, "width", &jswidth) &&
    JS_GetProperty(cx, tmp, "center", &jrcenter) &&
    JS_GetProperty(cx, tmp, "type", &jrtype) &&
    JS::ToNumber(cx, jsleft, &left) &&
    JS::ToNumber(cx, jright, &right) &&
    JS::ToNumber(cx, jswidth, &width) &&
    JS::ToNumber(cx, jrcenter, &center) &&
    JS::ToInt32(cx, jrtype, &type) ;
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");

    DHorizontalLayout::Type hType = DHorizontalLayout::Type(type);
    switch (hType)
    {
        case DHorizontalLayout::Type::L_R:
        {
            *horizontal = DHorizontalLayout_L_R(left, right);
        }
        break;
        case DHorizontalLayout::Type::L_W:
        {
            *horizontal = DHorizontalLayout_L_W(left, width);
        }
        break;
        case DHorizontalLayout::Type::R_W:
        {
            *horizontal = DHorizontalLayout_R_W(right, width);
        }
        break;
        case DHorizontalLayout::Type::W_C:
        {
            *horizontal = DHorizontalLayout_W_C(width, center);
        }
        break;
        case DHorizontalLayout::Type::NW_C:
        {
            *horizontal = DHorizontalLayout_NW_C(left, center);
        }
        break;
        default:
        break;
    }
    
    
    return true;
    
}
bool jsval_to_dverticallayout(JSContext *cx, JS::HandleValue v, CrossApp::DVerticalLayout* vertical){
    
    JS::RootedObject tmp(cx);
    JS::RootedValue jstop(cx);
    JS::RootedValue jsbottom(cx);
    JS::RootedValue jsheight(cx);
    JS::RootedValue jrcenter(cx);
    JS::RootedValue jrtype(cx);
    double top,bottom,height,center;
    int type = 0;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "top", &jstop) &&
    JS_GetProperty(cx, tmp, "bottom", &jsbottom) &&
    JS_GetProperty(cx, tmp, "height", &jsheight) &&
    JS_GetProperty(cx, tmp, "center", &jrcenter) &&
     JS_GetProperty(cx, tmp, "type", &jrtype) &&
    JS::ToNumber(cx, jstop, &top) &&
    JS::ToNumber(cx, jsbottom, &bottom) &&
    JS::ToNumber(cx, jsheight, &height) &&
    JS::ToNumber(cx, jrcenter, &center) &&
    JS::ToInt32(cx, jrtype, &type) ;
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");

    DVerticalLayout::Type vType = DVerticalLayout::Type(type);
    switch (vType)
    {
        case DVerticalLayout::Type::T_B:
        {
           *vertical = DVerticalLayout_T_B(top,bottom);
        }
        break;
        case DVerticalLayout::Type::T_H:
        {
            *vertical = DVerticalLayout_T_H(top,height);
        }
        break;
        case DVerticalLayout::Type::B_H:
        {
            *vertical = DVerticalLayout_B_H(bottom,height);
        }
        break;
        case DVerticalLayout::Type::H_C:
        {
            *vertical = DVerticalLayout_H_C(height,center);
        }
        break;
        case DVerticalLayout::Type::NH_C:
        {
            *vertical = DVerticalLayout_NH_C(top,center);
        }
            break;
        default:
        break;
    }
    
    
    return true;
    
    
}

bool jsval_to_dlayout(JSContext *cx, JS::HandleValue v, CrossApp::DLayout* layout){

    JS::RootedObject jsobj(cx);
    JS::RootedValue jshorizontal(cx);
    JS::RootedValue jsvertical(cx);
    
    bool ok = v.isObject() && JS_ValueToObject( cx, v, &jsobj );
    JSB_PRECONDITION3( ok, cx, false, "Error converting value to object");
    
    JS_GetProperty(cx, jsobj, "horizontal", &jshorizontal) &&
    JS_GetProperty(cx, jsobj, "vertical", &jsvertical);
    

    CrossApp::DHorizontalLayout cohorizontal;
    ok = jsval_to_dhorizontallayout(cx,jshorizontal,&cohorizontal);
    CrossApp::DVerticalLayout covertical;
    ok = jsval_to_dverticallayout(cx,jsvertical,&covertical);
    layout->horizontal = cohorizontal;
    layout->vertical = covertical;
    
    return true;
}

bool jsval_to_cafont(JSContext *cx, JS::HandleValue v, CrossApp::CAFont* ret){
    
    JS::RootedObject tmp(cx);
    JS::RootedValue jsbold(cx);
    JS::RootedValue jsunderLine(cx);
    JS::RootedValue jsdeleteLine(cx);
    JS::RootedValue jsitalics(cx);
    JS::RootedValue jsfontSize(cx);
    JS::RootedValue jscolor(cx);
    JS::RootedValue jsfontName(cx);
    
    bool bold,underLine,deleteLine,italics;
    double fontSize;
    CAColor4B color;
    std::string fontName;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx,  v, &tmp) &&
    JS_GetProperty(cx, tmp, "bold", &jsbold) &&
    JS_GetProperty(cx, tmp, "underLine", &jsunderLine) &&
    JS_GetProperty(cx, tmp, "deleteLine", &jsdeleteLine) &&
    JS_GetProperty(cx, tmp, "italics", &jsitalics) &&
    JS_GetProperty(cx, tmp, "fontSize", &jsfontSize) &&
    JS_GetProperty(cx, tmp, "color", &jscolor) &&
    JS_GetProperty(cx, tmp, "fontName", &jsfontName);
    bold = JS::ToBoolean(jsbold);
    underLine = JS::ToBoolean(jsunderLine);
    deleteLine = JS::ToBoolean(jsdeleteLine);
    italics = JS::ToBoolean(jsitalics);
    ok = JS::ToNumber(cx, jsfontSize, &fontSize);
    ok = jsval_to_cacolor4b(cx, jscolor, &color);
    ok = jsval_to_std_string(cx, jsfontName, &fontName);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->bold = bold;
    ret->underLine = underLine;
    ret->deleteLine = deleteLine;
    ret->italics = italics;
    ret->fontSize = fontSize;
    ret->color = color;
    ret->fontName = fontName;
    
    return true;
}
bool jsval_to_cacolor4b(JSContext *cx, JS::HandleValue v, CAColor4B* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsr(cx);
    JS::RootedValue jsg(cx);
    JS::RootedValue jsb(cx);
    JS::RootedValue jsa(cx);
    
    double r, g, b, a;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx,  v, &tmp) &&
    JS_GetProperty(cx, tmp, "r", &jsr) &&
    JS_GetProperty(cx, tmp, "g", &jsg) &&
    JS_GetProperty(cx, tmp, "b", &jsb) &&
    JS_GetProperty(cx, tmp, "a", &jsa) &&
    JS::ToNumber(cx, jsr, &r) &&
    JS::ToNumber(cx, jsg, &g) &&
    JS::ToNumber(cx, jsb, &b) &&
    JS::ToNumber(cx, jsa, &a);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->r = (GLubyte)r;
    ret->g = (GLubyte)g;
    ret->b = (GLubyte)b;
    ret->a = (GLubyte)a;
    return true;
}

bool jsval_to_cacolor4f(JSContext *cx, JS::HandleValue v, CrossApp::CAColor4F * ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsr(cx);
    JS::RootedValue jsg(cx);
    JS::RootedValue jsb(cx);
    JS::RootedValue jsa(cx);
    double r, g, b, a;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_GetProperty(cx, tmp, "r", &jsr) &&
    JS_GetProperty(cx, tmp, "g", &jsg) &&
    JS_GetProperty(cx, tmp, "b", &jsb) &&
    JS_GetProperty(cx, tmp, "a", &jsa) &&
    JS::ToNumber(cx, jsr, &r) &&
    JS::ToNumber(cx, jsg, &g) &&
    JS::ToNumber(cx, jsb, &b) &&
    JS::ToNumber(cx, jsa, &a);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    ret->r = (float)r / 255;
    ret->g = (float)g / 255;
    ret->b = (float)b / 255;
    ret->a = (float)a / 255;
    return true;
}

bool jsval_cacolor_to_opacity(JSContext *cx, JS::HandleValue v, int32_t* ret) {
    JS::RootedObject tmp(cx);
    JS::RootedValue jsa(cx);
    
    double a;
    bool ok = v.isObject() &&
    JS_ValueToObject(cx, v, &tmp) &&
    JS_LookupProperty(cx, tmp, "a", &jsa) &&
    !jsa.isUndefined() &&
    JS::ToNumber(cx, jsa, &a);
    
    if (ok) {
        *ret = (int32_t)a;
        return true;
    }
    else return false;
}

bool jsval_to_cavalue(JSContext* cx, JS::HandleValue v, CrossApp::CAValue* ret)
{
    if (v.isObject())
    {
        JS::RootedObject jsobj(cx, v.toObjectOrNull());
        CCASSERT(jsb_get_js_proxy(jsobj) == nullptr, "Native object should be added!");
        if (!JS_IsArrayObject(cx, jsobj))
        {
            // It's a normal js object.
            CAValueMap dictVal;
            bool ok = jsval_to_cavaluemap(cx, v, &dictVal);
            if (ok)
            {
                *ret = CAValue(dictVal);
            }
        }
        else {
            // It's a js array object.
            CAValueVector arrVal;
            bool ok = jsval_to_cavaluevector(cx, v, &arrVal);
            if (ok)
            {
                *ret = CAValue(arrVal);
            }
        }
    }
    else if (v.isString())
    {
        JSStringWrapper valueWapper(v.toString(), cx);
        *ret = CAValue(valueWapper.get());
    }
    else if (v.isNumber())
    {
        double number = 0.0;
        bool ok = JS::ToNumber(cx, v, &number);
        if (ok) {
            *ret = CAValue(number);
        }
    }
    else if (v.isBoolean())
    {
        bool boolVal = JS::ToBoolean(v);
        *ret = CAValue(boolVal);
    }
    else {
        CCASSERT(false, "not supported type");
    }
    
    return true;
}

bool jsval_to_cavaluemap(JSContext* cx, JS::HandleValue v, CrossApp::CAValueMap* ret)
{
    if (v.isNullOrUndefined())
    {
        return true;
    }
    
    JS::RootedObject tmp(cx, v.toObjectOrNull());
    if (!tmp) {
        CCLOG("%s", "jsval_to_ccvaluemap: the jsval is not an object.");
        return false;
    }
    
    JS::RootedObject it(cx, JS_NewPropertyIterator(cx, tmp));
    
    CAValueMap& dict = *ret;
    
    while (true)
    {
        JS::RootedId idp(cx);
        JS::RootedValue key(cx);
        if (! JS_NextProperty(cx, it, idp.address()) || ! JS_IdToValue(cx, idp, &key)) {
            return false; // error
        }
        
        if (key.isNullOrUndefined()) {
            break; // end of iteration
        }
        
        if (!key.isString()) {
            continue; // ignore integer properties
        }
        
        JSStringWrapper keyWrapper(key.toString(), cx);
        
        JS::RootedValue value(cx);
        JS_GetPropertyById(cx, tmp, idp, &value);
        if (value.isObject())
        {
            JS::RootedObject jsobj(cx, value.toObjectOrNull());
            CCASSERT(jsb_get_js_proxy(jsobj) == nullptr, "Native object should be added!");
            if (!JS_IsArrayObject(cx, jsobj))
            {
                // It's a normal js object.
                CAValueMap dictVal;
                bool ok = jsval_to_cavaluemap(cx, value, &dictVal);
                if (ok)
                {
                    dict.insert(CAValueMap::value_type(keyWrapper.get(), CAValue(dictVal)));
                }
            }
            else {
                // It's a js array object.
                CAValueVector arrVal;
                bool ok = jsval_to_cavaluevector(cx, value, &arrVal);
                if (ok)
                {
                    dict.insert(CAValueMap::value_type(keyWrapper.get(), CAValue(arrVal)));
                }
            }
        }
        else if (value.isString())
        {
            JSStringWrapper valueWapper(value.toString(), cx);
            dict.insert(CAValueMap::value_type(keyWrapper.get(), CAValue(valueWapper.get())));
        }
        else if (value.isNumber())
        {
            double number = 0.0;
            bool ok = JS::ToNumber(cx, value, &number);
            if (ok) {
                dict.insert(CAValueMap::value_type(keyWrapper.get(), CAValue(number)));
            }
        }
        else if (value.isBoolean())
        {
            bool boolVal = JS::ToBoolean(value);
            dict.insert(CAValueMap::value_type(keyWrapper.get(), CAValue(boolVal)));
        }
        else {
            CCASSERT(false, "not supported type");
        }
    }
    
    return true;
}

bool jsval_to_ccvaluemapintkey(JSContext* cx, JS::HandleValue v, CrossApp::CAValueMapIntKey* ret)
{
    if (v.isNullOrUndefined())
    {
        return true;
    }
    
    JS::RootedObject tmp(cx, v.toObjectOrNull());
    if (!tmp) {
        CCLOG("%s", "jsval_to_ccvaluemap: the jsval is not an object.");
        return false;
    }
    
    JS::RootedObject it(cx, JS_NewPropertyIterator(cx, tmp));
    
    CAValueMapIntKey& dict = *ret;
    
    while (true)
    {
        JS::RootedId idp(cx);
        JS::RootedValue key(cx);
        if (! JS_NextProperty(cx, it, idp.address()) || ! JS_IdToValue(cx, idp, &key)) {
            return false; // error
        }
        
        if (key.isNullOrUndefined()) {
            break; // end of iteration
        }
        
        if (!key.isString()) {
            continue; // ignore integer properties
        }
        
        int keyVal = key.toInt32();
        
        JS::RootedValue value(cx);
        JS_GetPropertyById(cx, tmp, idp, &value);
        if (value.isObject())
        {
            JS::RootedObject jsobj(cx, value.toObjectOrNull());
            CCASSERT(jsb_get_js_proxy(jsobj) == nullptr, "Native object should be added!");
            if (!JS_IsArrayObject(cx, jsobj))
            {
                // It's a normal js object.
                CAValueMap dictVal;
                bool ok = jsval_to_cavaluemap(cx, value, &dictVal);
                if (ok)
                {
                    dict.insert(CAValueMapIntKey::value_type(keyVal, CAValue(dictVal)));
                }
            }
            else {
                // It's a js array object.
                CAValueVector arrVal;
                bool ok = jsval_to_cavaluevector(cx, value, &arrVal);
                if (ok)
                {
                    dict.insert(CAValueMapIntKey::value_type(keyVal, CAValue(arrVal)));
                }
            }
        }
        else if (value.isString())
        {
            JSStringWrapper valueWapper(value.toString(), cx);
            dict.insert(CAValueMapIntKey::value_type(keyVal, CAValue(valueWapper.get())));
        }
        else if (value.isNumber())
        {
            double number = 0.0;
            bool ok = JS::ToNumber(cx, value, &number);
            if (ok) {
                dict.insert(CAValueMapIntKey::value_type(keyVal, CAValue(number)));
            }
        }
        else if (value.isBoolean())
        {
            bool boolVal = JS::ToBoolean(value);
            dict.insert(CAValueMapIntKey::value_type(keyVal, CAValue(boolVal)));
        }
        else {
            CCASSERT(false, "not supported type");
        }
    }
    
    return true;
}

bool jsval_to_cavaluevector(JSContext* cx, JS::HandleValue v, CrossApp::CAValueVector* ret)
{
    JS::RootedObject jsArr(cx);
    bool ok = v.isObject() && JS_ValueToObject( cx, v, &jsArr );
    JSB_PRECONDITION3( ok, cx, false, "Error converting value to object");
    JSB_PRECONDITION3( jsArr && JS_IsArrayObject( cx, jsArr),  cx, false, "Object must be an array");
    
    uint32_t len = 0;
    JS_GetArrayLength(cx, jsArr, &len);
    
    for (uint32_t i=0; i < len; i++)
    {
        JS::RootedValue value(cx);
        if (JS_GetElement(cx, jsArr, i, &value))
        {
            if (value.isObject())
            {
                JS::RootedObject jsobj(cx, value.toObjectOrNull());
                CCASSERT(jsb_get_js_proxy(jsobj) == nullptr, "Native object should be added!");
                
                if (!JS_IsArrayObject(cx, jsobj))
                {
                    // It's a normal js object.
                    CAValueMap dictVal;
                    ok = jsval_to_cavaluemap(cx, value, &dictVal);
                    if (ok)
                    {
                        ret->push_back(CAValue(dictVal));
                    }
                }
                else {
                    // It's a js array object.
                    CAValueVector arrVal;
                    ok = jsval_to_cavaluevector(cx, value, &arrVal);
                    if (ok)
                    {
                        ret->push_back(CAValue(arrVal));
                    }
                }
            }
            else if (value.isString())
            {
                JSStringWrapper valueWapper(value.toString(), cx);
                ret->push_back(CAValue(valueWapper.get()));
            }
            else if (value.isNumber())
            {
                double number = 0.0;
                ok = JS::ToNumber(cx, value, &number);
                if (ok)
                {
                    ret->push_back(CAValue(number));
                }
            }
            else if (value.isBoolean())
            {
                bool boolVal = JS::ToBoolean(value);
                ret->push_back(CAValue(boolVal));
            }
            else
            {
                CCASSERT(false, "not supported type");
            }
        }
    }
    
    return true;
}

bool jsval_to_ssize( JSContext *cx, JS::HandleValue vp, ssize_t* size)
{
    bool ret = false;
    int32_t sizeInt32 = 0;
    ret = jsval_to_int32(cx, vp, &sizeInt32);
    *size = sizeInt32;
    return ret;
}

bool jsval_to_std_vector_string( JSContext *cx, JS::HandleValue vp, std::vector<std::string>* ret)
{
    JS::RootedObject jsobj(cx);
    bool ok = vp.isObject() && JS_ValueToObject( cx, vp, &jsobj );
    JSB_PRECONDITION3( ok, cx, false, "Error converting value to object");
    JSB_PRECONDITION3( jsobj && JS_IsArrayObject( cx, jsobj),  cx, false, "Object must be an array");
    
    uint32_t len = 0;
    JS_GetArrayLength(cx, jsobj, &len);
    ret->reserve(len);
    for (uint32_t i=0; i < len; i++)
    {
        JS::RootedValue value(cx);
        if (JS_GetElement(cx, jsobj, i, &value))
        {
            if (value.isString())
            {
                JSStringWrapper valueWapper(value.toString(), cx);
                ret->push_back(valueWapper.get());
            }
            else
            {
                JS_ReportError(cx, "not supported type in array");
                return false;
            }
        }
    }
    
    return true;
}

bool jsval_to_matrix(JSContext *cx, JS::HandleValue vp, CrossApp::Mat4* ret)
{
    JS::RootedObject jsobj(cx);
    bool ok = vp.isObject() && JS_ValueToObject( cx, vp, &jsobj );
    JSB_PRECONDITION3( ok, cx, false, "Error converting value to object");
    JSB_PRECONDITION3( jsobj && JS_IsArrayObject( cx, jsobj),  cx, false, "Object must be an matrix");
    
    uint32_t len = 0;
    JS_GetArrayLength(cx, jsobj, &len);
    
    if (len != 16)
    {
        JS_ReportError(cx, "array length error: %d, was expecting 16", len);
    }
    
    for (uint32_t i=0; i < len; i++)
    {
        JS::RootedValue value(cx);
        if (JS_GetElement(cx, jsobj, i, &value))
        {
            if (value.isNumber())
            {
                double number = 0.0;
                ok = JS::ToNumber(cx, value, &number);
                if (ok)
                {
                    ret->m[i] = static_cast<float>(number);
                }
            }
            else
            {
                JS_ReportError(cx, "not supported type in matrix");
                return false;
            }
        }
    }
    
    return true;
}

bool jsval_to_vector2(JSContext *cx, JS::HandleValue vp, CrossApp::DPoint* ret)
{
    JS::RootedObject tmp(cx);
    JS::RootedValue jsx(cx);
    JS::RootedValue jsy(cx);
    double x, y;
    bool ok = vp.isObject() &&
    JS_ValueToObject(cx, vp, &tmp) &&
    JS_GetProperty(cx, tmp, "x", &jsx) &&
    JS_GetProperty(cx, tmp, "y", &jsy) &&
    JS::ToNumber(cx, jsx, &x) &&
    JS::ToNumber(cx, jsy, &y) &&
    !isnan((float)x) && !isnan((float)y);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->x = (float)x;
    ret->y = (float)y;
    return true;
}

bool jsval_to_blendfunc(JSContext *cx, JS::HandleValue vp, CrossApp::BlendFunc* ret)
{
    JS::RootedObject tmp(cx);
    JS::RootedValue jssrc(cx);
    JS::RootedValue jsdst(cx);
    double src, dst;
    bool ok = vp.isObject() &&
    JS_ValueToObject(cx, vp, &tmp) &&
    JS_GetProperty(cx, tmp, "src", &jssrc) &&
    JS_GetProperty(cx, tmp, "dst", &jsdst) &&
    JS::ToNumber(cx, jssrc, &src) &&
    JS::ToNumber(cx, jsdst, &dst);
    
    JSB_PRECONDITION3(ok, cx, false, "Error processing arguments");
    
    ret->src = (unsigned int)src;
    ret->dst = (unsigned int)dst;
    return true;
}

bool jsval_to_vector_vec2(JSContext* cx, JS::HandleValue v, std::vector<CrossApp::DPoint>* ret)
{
    JS::RootedObject jsArr(cx);
    bool ok = v.isObject() && JS_ValueToObject( cx, v, &jsArr );
    JSB_PRECONDITION3( ok, cx, false, "Error converting value to object");
    JSB_PRECONDITION3( jsArr && JS_IsArrayObject( cx, jsArr),  cx, false, "Object must be an array");
    
    uint32_t len = 0;
    JS_GetArrayLength(cx, jsArr, &len);
    ret->reserve(len);
    
    for (uint32_t i=0; i < len; i++)
    {
        JS::RootedValue value(cx);
        if (JS_GetElement(cx, jsArr, i, &value))
        {
            CrossApp::DPoint vec2;
            ok &= jsval_to_vector2(cx, value, &vec2);
            ret->push_back(vec2);
        }
    }
    return ok;
}

bool jsval_to_std_map_string_string(JSContext* cx, JS::HandleValue v, std::map<std::string, std::string>* ret)
{
    if (v.isNullOrUndefined())
    {
        return true;
    }
    
    JS::RootedObject tmp(cx, v.toObjectOrNull());
    if (!tmp) 
    {
        CCLOG("%s", "jsval_to_std_map_string_string: the jsval is not an object.");
        return false;
    }
    
    JS::RootedObject it(cx, JS_NewPropertyIterator(cx, tmp));
    
    std::map<std::string, std::string>& dict = *ret;
    
    while (true)
    {
        JS::RootedId idp(cx);
        JS::RootedValue key(cx);
        if (! JS_NextProperty(cx, it, idp.address()) || ! JS_IdToValue(cx, idp, &key)) 
        {
            return false; // error
        }
        
        if (key.isNullOrUndefined()) 
        {
            break; // end of iteration
        }
        
        if (!key.isString()) 
        {
            continue; // only take account of string key
        }
        
        JSStringWrapper keyWrapper(key.toString(), cx);
        
        JS::RootedValue value(cx);
        JS_GetPropertyById(cx, tmp, idp, &value);
        if (value.isString())
        {
            JSStringWrapper valueWapper(value.toString(), cx);
            dict[keyWrapper.get()] = valueWapper.get();
        }
        else 
        {
            CCASSERT(false, "jsval_to_std_map_string_string: not supported map type");
        }
    }
    
    return true;
}

//// From native type to jsval
jsval int8_to_jsval( JSContext *cx, int8_t number )
{
    return INT_TO_JSVAL(number);
}

jsval int32_to_jsval( JSContext *cx, int32_t number )
{
    return INT_TO_JSVAL(number);
}

jsval uint32_to_jsval( JSContext *cx, uint32_t number )
{
    return UINT_TO_JSVAL(number);
}

jsval ushort_to_jsval( JSContext *cx, unsigned short number )
{
    return UINT_TO_JSVAL(number);
}

jsval std_string_to_jsval(JSContext* cx, const std::string& v)
{
    return c_string_to_jsval(cx, v.c_str(), v.size());
}

jsval c_string_to_jsval(JSContext* cx, const char* v, size_t length)
{
    if (v == NULL)
    {
        return JSVAL_NULL;
    }
    if (length == -1)
    {
        length = strlen(v);
    }
    
    JSB_AUTOCOMPARTMENT_WITH_GLOBAL_OBJCET
    
    if (0 == length)
    {
        auto emptyStr = JS_NewStringCopyZ(cx, "");
        return STRING_TO_JSVAL(emptyStr);
    }
    
    jsval ret = JSVAL_NULL;

    int utf16_size = 0;
    const jschar* strUTF16 = (jschar*)cc_utf8_to_utf16(v, (int)length, &utf16_size);
    
    if (strUTF16 && utf16_size > 0) {
        JSString* str = JS_NewUCStringCopyN(cx, strUTF16, (size_t)utf16_size);
        if (str) {
            ret = STRING_TO_JSVAL(str);
        }
        delete[] strUTF16;
    }

    return ret;
}

jsval u_char_to_jsval(JSContext* cx, const unsigned char* v, size_t length)
{
    if (v == NULL)
    {
        return JSVAL_NULL;
    }
    if (length == -1)
    {
        length = strlen((const char*)v);
    }

    JSB_AUTOCOMPARTMENT_WITH_GLOBAL_OBJCET
    
    if (0 == length)
    {
        auto emptyStr = JS_NewStringCopyZ(cx, "");
        return STRING_TO_JSVAL(emptyStr);
    }
    
    jsval ret = JSVAL_NULL;
    
    const jschar* strUTF16 = (jschar*)v;
    
    if (strUTF16 && length > 0) {
        JSString* str = JS_NewUCStringCopyN(cx, strUTF16, length);
        if (str) {
            ret = STRING_TO_JSVAL(str);
        }
        delete[] strUTF16;
    }
    
    return ret;
}

jsval dpoint_to_jsval(JSContext* cx, const DPoint& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", v.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "y", v.y, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}
jsval dpoint3d_to_jsval(JSContext* cx, const CrossApp::DPoint3D& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", v.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "y", v.y, JSPROP_ENUMERATE | JSPROP_PERMANENT)&&
    JS_DefineProperty(cx, tmp, "z", v.z, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval ccacceleration_to_jsval(JSContext* cx, const CAAcceleration& v)
{
    JSB_AUTOCOMPARTMENT_WITH_GLOBAL_OBJCET

    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", v.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "y", v.y, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "z", v.z, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "timestamp", v.timestamp, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}
jsval viodpointe_to_javal(JSContext *cx, JS::HandleValue v, void* context)
{
    if (nullptr != context) {
//        ***
    }
    
     return JSVAL_NULL;
}
jsval drect_to_jsval(JSContext* cx, const DRect& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", v.origin.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "y", v.origin.y, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "width", v.size.width, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "height", v.size.height, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval dsize_to_jsval(JSContext* cx, const DSize& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "width", v.width, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "height", v.height, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}
jsval dhorizontallayout_to_jsval( JSContext *cx, const CrossApp::DHorizontalLayout& v){
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "left", v.left, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "right", v.right, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "width", v.width, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "center", v.center, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }

    return JSVAL_NULL;
}

jsval dverticallayout_to_jsval(JSContext *cx, const CrossApp::DVerticalLayout& v){

    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "top", v.top, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "bottom", v.bottom, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "height", v.height, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "center", v.center, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}
jsval dlayout_to_jsval(JSContext *cx, const CrossApp::DLayout& v){
    
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    JS::RootedValue dictHorizontal(cx);
    JS::RootedValue dictVertical(cx);
    dictHorizontal = dhorizontallayout_to_jsval(cx,v.horizontal);
    
    dictVertical = dverticallayout_to_jsval(cx,v.vertical);
    
    JS_SetProperty(cx, tmp, "horizontal", dictHorizontal);
    JS_SetProperty(cx, tmp, "vertical", dictVertical);
    
    return OBJECT_TO_JSVAL(tmp);
}
jsval cafont_to_jsval(JSContext *cx, const CrossApp::CAFont& v){

    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    JS::RootedValue color(cx);
    JS::RootedValue fontName(cx);
    color = cacolor4b_to_jsval(cx, v.color);
    fontName = std_string_to_jsval(cx, v.fontName);
    
    bool ok = JS_DefineProperty(cx, tmp, "bold", v.bold, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "underLine", v.underLine, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "deleteLine", v.deleteLine, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "italics", v.italics, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "fontSize", v.fontSize, JSPROP_ENUMERATE | JSPROP_PERMANENT)&&
    JS_SetProperty(cx, tmp, "color", color) &&
    JS_SetProperty(cx, tmp, "fontName", fontName);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    
    return JSVAL_NULL;
}
jsval cacolor4b_to_jsval(JSContext* cx, const CAColor4B& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "r", (int32_t)v.r, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "g", (int32_t)v.g, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "b", (int32_t)v.b, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "a", (int32_t)v.a, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval cacolor4f_to_jsval(JSContext* cx, const CAColor4F& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "r", (int32_t)(v.r * 255), JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "g", (int32_t)(v.g * 255), JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "b", (int32_t)(v.b * 255), JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "a", (int32_t)(v.a * 255), JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval quaternion_to_jsval(JSContext* cx, const CrossApp::Quaternion& q)
{
    JS::RootedObject tmp(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));
    if(!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", q.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
        JS_DefineProperty(cx, tmp, "y", q.y, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
        JS_DefineProperty(cx, tmp, "z", q.z, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
        JS_DefineProperty(cx, tmp, "w", q.w, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if(ok)
        return OBJECT_TO_JSVAL(tmp);

    return JSVAL_NULL;
}

jsval cavalue_to_jsval(JSContext* cx, const CrossApp::CAValue& v)
{
    jsval ret = JSVAL_NULL;
    const CAValue& obj = v;
    
    switch (obj.getType())
    {
        case CAValue::Type::BOOLEAN:
            ret = BOOLEAN_TO_JSVAL(obj.asBool());
            break;
        case CAValue::Type::FLOAT:
        case CAValue::Type::DOUBLE:
            ret = DOUBLE_TO_JSVAL(obj.asDouble());
            break;
        case CAValue::Type::INTEGER:
            ret = INT_TO_JSVAL(obj.asInt());
            break;
        case CAValue::Type::STRING:
            ret = std_string_to_jsval(cx, obj.asString());
            break;
        case CAValue::Type::VECTOR:
            ret = cavaluevector_to_jsval(cx, obj.asValueVector());
            break;
        case CAValue::Type::MAP:
            ret = cavaluemap_to_jsval(cx, obj.asValueMap());
            break;
        case CAValue::Type::INT_KEY_MAP:
            ret = cavaluemapintkey_to_jsval(cx, obj.asIntKeyMap());
            break;
        default:
            break;
    }
    
    return ret;
}

jsval cavaluemap_to_jsval(JSContext* cx, const CrossApp::CAValueMap& v)
{
    JS::RootedObject jsRet(cx, JS_NewArrayObject(cx, 0));
    
    for (auto iter = v.begin(); iter != v.end(); ++iter)
    {
        JS::RootedValue dictElement(cx);
        
        std::string key = iter->first;
        const CAValue& obj = iter->second;
        
        switch (obj.getType())
        {
            case CAValue::Type::BOOLEAN:
                dictElement = BOOLEAN_TO_JSVAL(obj.asBool());
                break;
            case CAValue::Type::FLOAT:
            case CAValue::Type::DOUBLE:
                dictElement = DOUBLE_TO_JSVAL(obj.asDouble());
                break;
            case CAValue::Type::INTEGER:
                dictElement = INT_TO_JSVAL(obj.asInt());
                break;
            case CAValue::Type::STRING:
                dictElement = std_string_to_jsval(cx, obj.asString());
                break;
            case CAValue::Type::VECTOR:
                dictElement = cavaluevector_to_jsval(cx, obj.asValueVector());
                break;
            case CAValue::Type::MAP:
                dictElement = cavaluemap_to_jsval(cx, obj.asValueMap());
                break;
            case CAValue::Type::INT_KEY_MAP:
                dictElement = cavaluemapintkey_to_jsval(cx, obj.asIntKeyMap());
                break;
            default:
                break;
        }
        
        if (!key.empty())
        {
            JS_SetProperty(cx, jsRet, key.c_str(), dictElement);
        }
    }
    return OBJECT_TO_JSVAL(jsRet);
}

jsval cavaluemapintkey_to_jsval(JSContext* cx, const CrossApp::CAValueMapIntKey& v)
{
    JS::RootedObject jsRet(cx, JS_NewArrayObject(cx, 0));
    
    for (auto iter = v.begin(); iter != v.end(); ++iter)
    {
        JS::RootedValue dictElement(cx);
        std::stringstream keyss;
        keyss << iter->first;
        std::string key = keyss.str();
        
        const CAValue& obj = iter->second;
        
        switch (obj.getType())
        {
            case CAValue::Type::BOOLEAN:
                dictElement = BOOLEAN_TO_JSVAL(obj.asBool());
                break;
            case CAValue::Type::FLOAT:
            case CAValue::Type::DOUBLE:
                dictElement = DOUBLE_TO_JSVAL(obj.asDouble());
                break;
            case CAValue::Type::INTEGER:
                dictElement = INT_TO_JSVAL(obj.asInt());
                break;
            case CAValue::Type::STRING:
                dictElement = std_string_to_jsval(cx, obj.asString());
                break;
            case CAValue::Type::VECTOR:
                dictElement = cavaluevector_to_jsval(cx, obj.asValueVector());
                break;
            case CAValue::Type::MAP:
                dictElement = cavaluemap_to_jsval(cx, obj.asValueMap());
                break;
            case CAValue::Type::INT_KEY_MAP:
                dictElement = cavaluemapintkey_to_jsval(cx, obj.asIntKeyMap());
                break;
            default:
                break;
        }
        
        if (!key.empty())
        {
            JS_SetProperty(cx, jsRet, key.c_str(), dictElement);
        }
    }
    return OBJECT_TO_JSVAL(jsRet);
}

jsval cavaluevector_to_jsval(JSContext* cx, const CrossApp::CAValueVector& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, 0));
    
    int i = 0;
    for (const auto& obj : v)
    {
        JS::RootedValue arrElement(cx);
        
        switch (obj.getType())
        {
            case CAValue::Type::BOOLEAN:
                arrElement = BOOLEAN_TO_JSVAL(obj.asBool());
                break;
            case CAValue::Type::FLOAT:
            case CAValue::Type::DOUBLE:
                arrElement = DOUBLE_TO_JSVAL(obj.asDouble());
                break;
            case CAValue::Type::INTEGER:
                arrElement = INT_TO_JSVAL(obj.asInt());
                break;
            case CAValue::Type::STRING:
                arrElement = std_string_to_jsval(cx, obj.asString());
                break;
            case CAValue::Type::VECTOR:
                arrElement = cavaluevector_to_jsval(cx, obj.asValueVector());
                break;
            case CAValue::Type::MAP:
                arrElement = cavaluemap_to_jsval(cx, obj.asValueMap());
                break;
            case CAValue::Type::INT_KEY_MAP:
                arrElement = cavaluemapintkey_to_jsval(cx, obj.asIntKeyMap());
                break;
            default:
                break;
        }
        
        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
        ++i;
    }
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval ssize_to_jsval(JSContext *cx, ssize_t v)
{
    CCAssert(v < INT_MAX, "The size should not bigger than 32 bit (int32_t).");
    return int32_to_jsval(cx, static_cast<int>(v));
}

jsval std_vector_string_to_jsval( JSContext *cx, const std::vector<std::string>& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, v.size()));
    
    int i = 0;
    for (const std::string obj : v)
    {
        JS::RootedValue arrElement(cx);
        arrElement = std_string_to_jsval(cx, obj);
        
        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
        ++i;
    }
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval std_vector_char_to_jsval( JSContext *cx, const std::vector<char>& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, v.size()));
    
    int i = 0;
    for (const char obj : v)
    {
        JS::RootedValue arrElement(cx);
        arrElement = int8_to_jsval(cx, obj);
        
        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
        ++i;
    }
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval std_vector_int_to_jsval( JSContext *cx, const std::vector<int>& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, v.size()));
    
    int i = 0;
    for (const int obj : v)
    {
        JS::RootedValue arrElement(cx);
        arrElement = int32_to_jsval(cx, obj);
        
        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
        ++i;
    }
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval std_vector_float_to_jsval( JSContext *cx, const std::vector<float>& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, v.size()));

    int i = 0;
    for (const float obj : v)
    {
        JS::RootedValue arrElement(cx);
        arrElement = DOUBLE_TO_JSVAL(obj);

        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
        ++i;
    }
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval matrix_to_jsval(JSContext *cx, const CrossApp::Mat4& v)
{
    JS::RootedObject jsretArr(cx, JS_NewArrayObject(cx, 16));
    
    for (int i = 0; i < 16; i++) {
        JS::RootedValue arrElement(cx);
        arrElement = DOUBLE_TO_JSVAL(v.m[i]);
        
        if (!JS_SetElement(cx, jsretArr, i, arrElement)) {
            break;
        }
    }
    
    return OBJECT_TO_JSVAL(jsretArr);
}

jsval vector2_to_jsval(JSContext *cx, const CrossApp::DPoint& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "x", v.x, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "y", v.y, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval blendfunc_to_jsval(JSContext *cx, const CrossApp::BlendFunc& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject tmp(cx, JS_NewObject(cx, NULL, proto, parent));
    if (!tmp) return JSVAL_NULL;
    bool ok = JS_DefineProperty(cx, tmp, "src", (uint32_t)v.src, JSPROP_ENUMERATE | JSPROP_PERMANENT) &&
    JS_DefineProperty(cx, tmp, "dst", (uint32_t)v.dst, JSPROP_ENUMERATE | JSPROP_PERMANENT);
    if (ok) {
        return OBJECT_TO_JSVAL(tmp);
    }
    return JSVAL_NULL;
}

jsval std_map_string_string_to_jsval(JSContext* cx, const std::map<std::string, std::string>& v)
{
    JS::RootedObject proto(cx);
    JS::RootedObject parent(cx);
    JS::RootedObject jsRet(cx, JS_NewObject(cx, NULL, proto, parent));
    
    for (auto iter = v.begin(); iter != v.end(); ++iter)
    {
        JS::RootedValue element(cx);
        
        std::string key = iter->first;
        std::string obj = iter->second;
        
        element = std_string_to_jsval(cx, obj);
        
        if (!key.empty())
        {
            JS_SetProperty(cx, jsRet, key.c_str(), element);
        }
    }
    return OBJECT_TO_JSVAL(jsRet);
}
