// Oleg Kotov

#include "biquad.h"

#include <float.h>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

Biquad::Biquad()
{
    setNormalizedCoefficients( 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f );
    reset();
}

Biquad::~Biquad() = default;

float Biquad::process( float sample )
{
    float x1 = m_x1;
    float x2 = m_x2;
    float y1 = m_y1;
    float y2 = m_y2;

    float b0 = m_b0;
    float b1 = m_b1;
    float b2 = m_b2;
    float a1 = m_a1;
    float a2 = m_a2;

    float x = sample;
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;

    if ( x1 == 0.0f && x2 == 0.0f && ( y1 != 0.0f || y2 != 0.0f ) && fabs( y1 ) < FLT_MIN && fabs( y2 ) < FLT_MIN ) 
    {
        y1 = y2 = 0.0f;
    }

    m_x1 = x1;
    m_x2 = x2;
    m_y1 = y1;
    m_y2 = y2;

    return y;
}

void Biquad::reset() { m_x1 = m_x2 = m_y1 = m_y2 = 0.0f; }

void Biquad::setNormalizedCoefficients( float b0, float b1, float b2, float a0, float a1, float a2 )
{
    float a0Inverse = 1 / a0;

    m_b0 = b0 * a0Inverse;
    m_b1 = b1 * a0Inverse;
    m_b2 = b2 * a0Inverse;
    m_a1 = a1 * a0Inverse;
    m_a2 = a2 * a0Inverse;
}

void Biquad::setPeakingParams( float frequency, float Q, float dbGain )
{
    frequency = std::max( 0.0f, std::min( frequency, 1.0f ) );

    Q = std::max( 0.0f, Q );

    float A = pow( 10.0f, dbGain / 40.0f );

    if ( frequency > 0.0f && frequency < 1.0f )
    {
        if ( Q > 0.0f )
        {
            float w0 = M_PI * frequency;
            float alpha = sin( w0 ) / ( 2.0f * Q );
            float k = cos( w0 );

            float b0 =  1.0f + alpha * A;
            float b1 = -2.0f * k;
            float b2 =  1.0f - alpha * A;
            float a0 =  1.0f + alpha / A;
            float a1 = -2.0f * k;
            float a2 =  1.0f - alpha / A;

            setNormalizedCoefficients( b0, b1, b2, a0, a1, a2 );
        }
        else
        {
            setNormalizedCoefficients( A * A, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f );
        }
    }
    else
    {
        setNormalizedCoefficients( 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f );
    }
}

