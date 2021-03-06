#pragma once

// drawing backend variants
#define SVGREN_BACKEND_CAIRO 1
#define SVGREN_BACKEND_AGG 2
#define SVGREN_BACKEND_SKIA 3

#ifndef SVGREN_BACKEND
#   define SVGREN_BACKEND SVGREN_BACKEND_CAIRO
#endif

namespace svgren{

typedef float real;

typedef uint32_t pixel;

}
