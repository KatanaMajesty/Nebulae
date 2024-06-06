#include "Shader.h"

#include "../common/Defines.h"

namespace Neb::nri
{

    LPVOID Shader::GetBinaryPointer() const
    {
        NEB_ASSERT(HasBinary());
        return m_binary->GetBufferPointer();
    }

    SIZE_T Shader::GetBinarySize() const
    {
        NEB_ASSERT(HasBinary());
        return m_binary->GetBufferSize();
    }

} // Neb::nri namespace
