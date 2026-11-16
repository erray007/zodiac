// Stands in for a closed third-party DLL (BASS, FMOD): a plain shared library, no Zodiac module. Built twice: as vendor_base.dll
// (VENDOR_BASE defined), exporting vendor_base_factor() = 7, and as vendor_lib.dll, exporting vendor_lib_value(value) = value*vendor_base_factor()
// and so importing vendor_base.dll.
#define VENDOR_EXPORT extern "C" __declspec(dllexport)

#ifdef VENDOR_BASE
VENDOR_EXPORT int vendor_base_factor(void)
{
return 7;
}
#else
extern "C" __declspec(dllimport) int vendor_base_factor(void);

VENDOR_EXPORT int vendor_lib_value(int value)
{
return value*vendor_base_factor();
}
#endif
