// Oleg Kotov

#pragma once

class Biquad
{
public:

    Biquad();
    ~Biquad();

    float process( float sample );

    void setPeakingParams( float frequency, float Q, float dbGain );

    bool hasTail() const { return m_y1 || m_y2 || m_x1 || m_x2; }

    void reset();

private:

    void setNormalizedCoefficients( float b0, float b1, float b2, float a0, float a1, float a2 );

    float m_b0;
    float m_b1;
    float m_b2;
    float m_a1;
    float m_a2;

    float m_x1;
    float m_x2;
    float m_y1;
    float m_y2;
};

