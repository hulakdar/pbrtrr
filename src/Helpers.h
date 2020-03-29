#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <exception>

// From DXSampleHelper.h 
// Source: https://github.com/Microsoft/DirectX-Graphics-Samples
inline void ThrowIfFailed( HRESULT aHResult )
{
	if ( FAILED( aHResult ) )
	{
		throw std::exception( );
	}
}