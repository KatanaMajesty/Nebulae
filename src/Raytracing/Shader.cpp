#include "Shader.h"

LPVOID NebShader::GetBinaryPointer() const
{
    //WARP_ASSERT(HasBinary());
    return m_Binary->GetBufferPointer();
}

SIZE_T NebShader::GetBinarySize() const
{
    //WARP_ASSERT(HasBinary());
    return m_Binary->GetBufferSize();
}