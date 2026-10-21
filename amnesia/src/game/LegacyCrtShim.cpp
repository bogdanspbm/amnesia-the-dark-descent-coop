/*
 * Link fixes for the prebuilt VS2010 /MD libraries (fltk.lib, angelscript.lib)
 * against the Universal CRT. Their stdio imports come from legacy_stdio_definitions.lib
 * (see Lux.vcxproj); fltk also references the C++ overload of _wopen that the old CRT
 * exported, while UCRT only exports the C one.
 */
#if defined(_MSC_VER) && _MSC_VER >= 1900 && defined(_M_IX86)
#pragma comment(linker, "/ALTERNATENAME:__imp_?_wopen@@YAHPB_WHH@Z=__imp___wopen")
#endif
