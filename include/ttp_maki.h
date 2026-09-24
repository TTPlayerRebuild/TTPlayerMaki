#pragma once
// Independent MAKI interpreter ABI. No GUI, Wasabi, STL or CRT ownership crosses
// this boundary. All calls on an instance must use its creating thread.
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#define TTP_MAKI_ABI 1u
#define TTP_MAKI_ENTRY "ttpGetMakiVM"
enum TtpMakiType { TTP_MAKI_VOID, TTP_MAKI_NUMBER, TTP_MAKI_STRING, TTP_MAKI_OBJECT };
typedef struct TtpMakiValue {
    uint32_t type;
    double number;
    const wchar_t* text;
    void* object;
} TtpMakiValue;
typedef struct TtpMakiHost {
    uint32_t size;
    void* context;
    // Resolve ALL imports before any script executes. Negative arity rejects.
    int (WINAPI *resolve)(void*, const GUID*, const wchar_t*);
    // Returned text is borrowed until this callback is invoked again.
    HRESULT (WINAPI *invoke)(void*, const GUID*, void*, const wchar_t*,
        const TtpMakiValue*, uint32_t, TtpMakiValue*);
    // Optional allocation contract for NEW/DELETE; the host owns objects.
    HRESULT (WINAPI *construct)(void*, const GUID*, void**);
    void (WINAPI *release)(void*, void*);
} TtpMakiHost;
#define TTP_MAKI_HOST_V1_SIZE offsetof(TtpMakiHost, construct)
typedef struct TtpMakiVM {
    uint32_t size, version;
    HRESULT (WINAPI *create)(const uint8_t*, uint32_t, const TtpMakiHost*, void* system, void**);
    void (WINAPI *destroy)(void*);
    // Text in result is borrowed until the next event/destroy. A failing event
    // disables this instance. completed reports MAKI's complete opcode.
    HRESULT (WINAPI *event)(void*, void*, const wchar_t*, const TtpMakiValue*, uint32_t,
        TtpMakiValue* result, BOOL* completed);
    // Optional diagnostic variant; same ownership/validation as create.
    HRESULT (WINAPI *create_checked)(const uint8_t*, uint32_t, const TtpMakiHost*, void*, void**, wchar_t*, uint32_t);
} TtpMakiVM;
#define TTP_MAKI_VM_V1_SIZE offsetof(TtpMakiVM, create_checked)
typedef HRESULT (WINAPI *TtpGetMakiVM)(uint32_t, TtpMakiVM*);
