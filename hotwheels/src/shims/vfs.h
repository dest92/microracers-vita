// Compatibilidad temporal: el VFS portable vive en winx86-dx.
#pragma once

#include "../../../winx86-dx/include/winx86-dx/io/vfs.h"

namespace hw::shims {
using Vfs = ::winx86::dx::io::Vfs;
}
