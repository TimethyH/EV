#pragma once
#include <string>
#include <Windows.h>

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}

// Convert a multi-byte character string (UTF-8) to a wide (UTF-16) encoded string.
inline std::wstring ConvertString(const std::string& utf8)
{
    if (utf8.empty())
        return {};

    int size_needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0
    );

    std::wstring result(size_needed, 0);

    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        result.data(),
        size_needed
    );

    return result;
}
inline std::string ConvertString(const std::wstring& utf16)
{
    if (utf16.empty())
        return {};

    int size_needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.data(),
        static_cast<int>(utf16.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    std::string result(size_needed, 0);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        utf16.data(),
        static_cast<int>(utf16.size()),
        result.data(),
        size_needed,
        nullptr,
        nullptr
    );

    return result;
}


inline std::wstring to_wstring(const std::string& s)
{
    return ConvertString(s);
}

inline const std::wstring& to_wstring(const std::wstring& s)
{
    return s;
}

inline std::wstring to_wstring(char c)
{
    return to_wstring(std::string(1, c));
}



// Currently FULLY copy pasted from Jeremiah van Oosten's DX12 tutorial:
namespace Math
{
    constexpr float PI = 3.1415926535897932384626433832795f;
    constexpr float _2PI = 2.0f * PI;
    // Convert radians to degrees.
    constexpr float Degrees(const float radians)
    {
        return radians * (180.0f / PI);
    }

    // Convert degrees to radians.
    constexpr float Radians(const float degrees)
    {
        return degrees * (PI / 180.0f);
    }

    template<typename T>
    inline T Deadzone(T val, T deadzone)
    {
        if (std::abs(val) < deadzone)
        {
            return T(0);
        }

        return val;
    }

    // Normalize a value in the range [min - max]
    template<typename T, typename U>
    inline T NormalizeRange(U x, U min, U max)
    {
        return T(x - min) / T(max - min);
    }

    // Shift and bias a value into another range.
    template<typename T, typename U>
    inline T ShiftBias(U x, U shift, U bias)
    {
        return T(x * bias) + T(shift);
    }

    /***************************************************************************
    * These functions were taken from the MiniEngine.
    * Source code available here:
    * https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Math/Common.h
    * Retrieved: January 13, 2016
    **************************************************************************/
    template <typename T>
    inline T AlignUpWithMask(T value, size_t mask)
    {
        return (T)(((size_t)value + mask) & ~mask);
    }

    template <typename T>
    inline T AlignDownWithMask(T value, size_t mask)
    {
        return (T)((size_t)value & ~mask);
    }

    template <typename T>
    inline T AlignUp(T value, size_t alignment)
    {
        return AlignUpWithMask(value, alignment - 1);
    }

    template <typename T>
    inline T AlignDown(T value, size_t alignment)
    {
        return AlignDownWithMask(value, alignment - 1);
    }

    template <typename T>
    inline bool IsAligned(T value, size_t alignment)
    {
        return 0 == ((size_t)value & (alignment - 1));
    }

    template <typename T>
    inline T DivideByMultiple(T value, size_t alignment)
    {
        return (T)((value + alignment - 1) / alignment);
    }
    /***************************************************************************/

    /**
    * Round up to the next highest power of 2.
    * @source: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    * @retrieved: January 16, 2016
    */
    inline uint32_t NextHighestPow2(uint32_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;

        return v;
    }

    /**
    * Round up to the next highest power of 2.
    * @source: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    * @retrieved: January 16, 2016
    */
    inline uint64_t NextHighestPow2(uint64_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;

        return v;
    }
}

#define STR1(x) #x
#define STR(x) STR1(x)
#define WSTR1(x) L##x
#define WSTR(x) WSTR1(x)
#define NAME_D3D12_OBJECT(x) x->SetName( WSTR(__FILE__ "(" STR(__LINE__) "): " L#x) )