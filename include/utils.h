// Oleg Kotov

#pragma once

#include <stdint.h>
#include <vector>

class String;

String md5( const String& str );
String format_bytes( uint64_t bytes );
String format_artists( std::vector<String>& artists );
String pad( int32_t n );
String format_duration( int32_t duration );
bool   isdigit( const char* str );
bool endsWith ( const char* base, const char* str );

template <class UserType>
UserType clamp( UserType value, UserType min, UserType max )
{
    if ( value >= max ) return max;
    if ( value <= min ) return min;
    return value;
}

template <class UserType>
UserType lerp( UserType start, UserType stop, UserType amount )
{
    return start + ( stop - start ) * amount;
}

