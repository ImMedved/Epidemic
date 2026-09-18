// Minimal DLL used to verify EngineBase dynamic-library ownership and symbol lookup contracts.

extern "C" __declspec(dllexport) int EpidemicPlatformTestSymbol()
{
    return 42;
}
