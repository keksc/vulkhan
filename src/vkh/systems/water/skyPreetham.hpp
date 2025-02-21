#pragma once

#include <glm/glm.hpp>


class SkyPreetham
{
public:
    static constexpr float s_kDefaultTurbidity{ 2.5f };

    /** @brief Properties of the sky model */
    struct Props
    {
        alignas(16) glm::vec3 sunDir;           ///< Normalized dir. to sun
                    float     turbidity;
        alignas(16) glm::vec3 A, B, C, D, E;
        alignas(16) glm::vec3 ZenithLum;
        alignas(16) glm::vec3 ZeroThetaSun;
    };

public:
    SkyPreetham(const glm::vec3& sunDir,
                float turbidity = SkyPreetham::s_kDefaultTurbidity);

    /**
     * @brief Recomputes its properties after parameters have been changed
     */
    void Update();

    void SetSunDirection(const glm::vec3& sunDir) {
        m_Props.sunDir = glm::normalize(sunDir);
    }
    void SetTurbidity(float t) { m_Props.turbidity = t; }

    glm::vec3 GetSunDirection() const { return m_Props.sunDir; }
    float GetTurbidity() const { return m_Props.turbidity; }

    const Props& GetProperties() const { return m_Props; }

private:
    void ComputePerezDistribution();
    void ComputeZenithLuminanceYxy(const float thetaSun);
    void ComputePerezLuminanceYxy(const float gamma);

private:
    Props m_Props;
};
