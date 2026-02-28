#pragma once

/*
 *  Copyright(c) 2020 Jeremiah van Oosten
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files(the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions :
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

 /**
  *  @file Light.h
  *  @date November 10, 2020
  *  @author Jeremiah van Oosten
  *
  *  @brief Light structures that use HLSL constant buffer padding rules.
  */

#include <DirectXMath.h>

namespace EV
{

    struct PointLight
    {
        PointLight()
            : positionWS(0.0f, 0.0f, 0.0f, 1.0f)
            , positionVS(0.0f, 0.0f, 0.0f, 1.0f)
            , color(1.0f, 1.0f, 1.0f, 1.0f)
            , ambient(0.01f)
            , constantAttenuation(1.0f)
            , linearAttenuation(0.0f)
            , quadraticAttenuation(0.0f)
        {
        }

        DirectX::XMFLOAT4 positionWS;  // Light position in world space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 positionVS;  // Light position in view space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 color;
        //----------------------------------- (16 byte boundary)
        float ambient;
        float constantAttenuation;
        float linearAttenuation;
        float quadraticAttenuation;
        //----------------------------------- (16 byte boundary)
        // Total:                              16 * 4 = 64 bytes
    };

    struct SpotLight
    {
        SpotLight()
            : positionWS(0.0f, 0.0f, 0.0f, 1.0f)
            , positionVS(0.0f, 0.0f, 0.0f, 1.0f)
            , directionWS(0.0f, 0.0f, 1.0f, 0.0f)
            , directionVS(0.0f, 0.0f, 1.0f, 0.0f)
            , color(1.0f, 1.0f, 1.0f, 1.0f)
            , ambient(0.01f)
            , spotAngle(DirectX::XM_PIDIV2)
            , constantAttenuation(1.0f)
            , linearAttenuation(0.0f)
            , quadraticAttenuation(0.0f)
        {
        }

        DirectX::XMFLOAT4 positionWS;  // Light position in world space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 positionVS;  // Light position in view space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 directionWS;  // Light direction in world space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 directionVS;  // Light direction in view space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 color;
        //----------------------------------- (16 byte boundary)
        float ambient;
        float spotAngle;
        float constantAttenuation;
        float linearAttenuation;
        //----------------------------------- (16 byte boundary)
        float quadraticAttenuation;
        float padding[3];
        //----------------------------------- (16 byte boundary)
        // Total:                              16 * 7 = 112 bytes
    };

    struct DirectionalLight
    {
        DirectionalLight()
            : directionWS(0.0f, 0.0f, 1.0f, 0.0f)
            , directionVS(0.0f, 0.0f, 1.0f, 0.0f)
            , color(1.0f, 1.0f, 1.0f, 1.0f)
			, position(5.0f,5.0f,-20.0f)
            , ambient(0.01f)
        {
        }

        DirectX::XMFLOAT4 directionWS;  // Light direction in world space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 directionVS;  // Light direction in view space.
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT4 color;
        //----------------------------------- (16 byte boundary)
        DirectX::XMFLOAT3 position;
        float ambient;
        //----------------------------------- (16 byte boundary)
        // Total:                              16 * 4 = 64 bytes
    };
}