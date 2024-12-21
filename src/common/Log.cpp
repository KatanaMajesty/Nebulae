#include "Log.h"

namespace Neb
{

    void Trace(EConsoleColor color, std::string_view msg)
    {
        BareConsole& console = BareConsole::Get();
        {
            std::scoped_lock _(console.OutputMutex);
            assert(SetConsoleTextAttribute(console.OutputHandle, static_cast<WORD>(color)) && "Failed to set console color");
            {
                std::println("{}", msg);
            }
            assert(SetConsoleTextAttribute(console.OutputHandle, static_cast<WORD>(console.CurrentColor)) && "Failed to set console color");
        }
    }

    void Trace(ETraceCategory category, std::string_view msg)
    {
        switch (category)
        {
        case ETraceCategory::Info: Trace(Neb::EConsoleColor::Gray, msg); break;
        case ETraceCategory::Warning: Trace(Neb::EConsoleColor::Yellow, msg); break;
        case ETraceCategory::Error: Trace(Neb::EConsoleColor::Red, msg); break;
        default: assert(false && "Undefined category");
        }
    }

    void TraceIf(bool expr, ETraceCategory category, std::string_view msg)
    {
        if (expr)
        {
            Trace(category, msg);
        }
    }

}; // Neb namespace