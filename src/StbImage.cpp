// Compile the bundled decoder once; keep its implementation out of application headers.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
