// TopoWeave for 3ds Max - SDK version helpers.
//
// 3ds Max 2022 (MAX_RELEASE 24000) added "localized" arguments and const
// qualifiers to several naming virtuals. These macros keep one code base
// compiling against 2021 and every newer SDK.
#pragma once

#include <plugapi.h>

#if MAX_RELEASE >= 24000
#define TW_MAX2022 1
#else
#define TW_MAX2022 0
#endif

#if TW_MAX2022
#define TW_GETCLASSNAME_SIG void GetClassName(MSTR& s, bool localized = true) const override
#define TW_SUBANIMNAME_SIG MSTR SubAnimName(int i, bool localized = true) override
#define TW_GETOBJECTNAME_SIG const MCHAR* GetObjectName(bool localized = true) const override
#else
#define TW_GETCLASSNAME_SIG void GetClassName(MSTR& s) override
#define TW_SUBANIMNAME_SIG MSTR SubAnimName(int i) override
#define TW_GETOBJECTNAME_SIG const MCHAR* GetObjectName() override
#endif
