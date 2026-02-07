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
  *  @file VertexTypes.h
  *  @date November 12, 2020
  *  @author Jeremiah van Oosten
  *
  *  @brief Vertex type definitions.
  */

#include <DirectXMath.h>

#include <d3d12.h>

namespace EV
{

    struct VertexPosition
    {
        VertexPosition() = default;

        explicit VertexPosition(const DirectX::XMFLOAT3& position)
            : position(position)
        {
        }

        explicit VertexPosition(DirectX::FXMVECTOR position)
        {
            DirectX::XMStoreFloat3(&(this->position), position);
        }

        DirectX::XMFLOAT3 position;

        static const D3D12_INPUT_LAYOUT_DESC inputLayout;
    private:
        static const int                      inputElementCount = 1;
        static const D3D12_INPUT_ELEMENT_DESC inputElements[inputElementCount];
    };

    struct VertexPositionNormalTangentBitangentTexture
    {
        VertexPositionNormalTangentBitangentTexture() = default;

        explicit VertexPositionNormalTangentBitangentTexture(const DirectX::XMFLOAT3& position,
            const DirectX::XMFLOAT3& normal,
            const DirectX::XMFLOAT3& texCoord,
            const DirectX::XMFLOAT3& tangent = { 0, 0, 0 },
            const DirectX::XMFLOAT3& bitangent = { 0, 0, 0 })
            : position(position)
            , normal(normal)
            , tangent(tangent)
            , bitangent(bitangent)
            , texCoord(texCoord)
        {
        }

        explicit VertexPositionNormalTangentBitangentTexture(DirectX::FXMVECTOR position, DirectX::FXMVECTOR normal,
            DirectX::FXMVECTOR texCoord,
            DirectX::GXMVECTOR tangent = { 0, 0, 0, 0 },
            DirectX::HXMVECTOR bitangent = { 0, 0, 0, 0 })
        {
            DirectX::XMStoreFloat3(&(this->position), position);
            DirectX::XMStoreFloat3(&(this->normal), normal);
            DirectX::XMStoreFloat3(&(this->tangent), tangent);
            DirectX::XMStoreFloat3(&(this->bitangent), bitangent);
            DirectX::XMStoreFloat3(&(this->texCoord), texCoord);
        }

        DirectX::XMFLOAT3 position;
        DirectX::XMFLOAT3 normal;
        DirectX::XMFLOAT3 tangent;
        DirectX::XMFLOAT3 bitangent;
        DirectX::XMFLOAT3 texCoord;

        static const D3D12_INPUT_LAYOUT_DESC inputLayout;
    private:
        static const int                      inputElementCount = 5; // TODO: make it back to 5 when adding tang and bitangent
        static const D3D12_INPUT_ELEMENT_DESC inputElements[inputElementCount];
    };
}  // namespace dx12lib
