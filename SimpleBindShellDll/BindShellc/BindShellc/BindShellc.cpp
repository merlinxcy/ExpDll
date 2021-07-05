// BindShellc.cpp : 定义 DLL 的导出函数。
//

#include "pch.h"
#include "framework.h"
#include "BindShellc.h"


// 这是导出变量的一个示例
BINDSHELLC_API int nBindShellc=0;

// 这是导出函数的一个示例。
BINDSHELLC_API int fnBindShellc(void)
{
    return 0;
}

// 这是已导出类的构造函数。
CBindShellc::CBindShellc()
{
    return;
}
