#include "Shader.h"

#include "../common/Assert.h"

namespace Neb::nri
{

    LPVOID Shader::GetBinaryPointer() const
    {
        NEB_ASSERT(HasBinary(), "Shader has no binary!");
        return m_binary->GetBufferPointer();
    }

    SIZE_T Shader::GetBinarySize() const
    {
        NEB_ASSERT(HasBinary(), "Shader has no binary!");
        return m_binary->GetBufferSize();
    }

} // Neb::nri namespace
