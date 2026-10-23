// API WinMM portable compartida; los adaptadores de Hot Wheels mantienen namespace hw.
#pragma once
#include "../../../winx86-dx/include/winx86-dx/winmm/winmm.h"
namespace hw { namespace winmm { using namespace ::winx86::dx::winmm; } }
