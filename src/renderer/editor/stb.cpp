// stb_image is already implemented in nvtt
//#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#if defined _MSC_VER && _MSC_VER == 1900 
#pragma warning(disable : 4312)
#endif
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
