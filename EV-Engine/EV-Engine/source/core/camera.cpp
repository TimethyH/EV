#include "core/camera.h"


#include <limits>

#include "DX12/command_list.h"
#include "DX12/effect_pso.h"

using namespace EV;
Camera::Camera()
	:m_viewDirty(true)
	,m_inverseViewDirty(true)
	,m_projectionDirty(true)
	,m_inverseProjectionDirty(true)
	,m_fov(45.0f)
	,m_aspect(1.0f)
	,m_zNear(0.1f)
	,m_zFar(100.0f)
{
	pData = static_cast<AlignedData*>(_aligned_malloc(sizeof(AlignedData), 16));
	pData->m_translation = DirectX::XMVectorZero();
	pData->m_rotation = DirectX::XMQuaternionIdentity();

}

Camera::~Camera()
{
	_aligned_free(pData);
}

void XM_CALLCONV Camera::SetLookAt(DirectX::FXMVECTOR& position, DirectX::FXMVECTOR& target, DirectX::FXMVECTOR& up) const
{
	pData->m_viewMatrix = DirectX::XMMatrixLookAtLH(position, target, up);
	pData->m_translation = position;
	pData->m_rotation = DirectX::XMQuaternionRotationMatrix(XMMatrixTranspose(pData->m_viewMatrix));

	m_inverseViewDirty = true;
	m_viewDirty = false;
}

DirectX::XMMATRIX Camera::GetViewMatrix() const
{
	if (m_viewDirty)
	{
		UpdateViewMatrix();
	}
	return pData->m_viewMatrix;
}

DirectX::XMMATRIX Camera::GetInverseViewMatrix() const
{
	if (m_inverseViewDirty)
	{
		pData->m_inverseViewMatrix = DirectX::XMMatrixInverse(nullptr, pData->m_viewMatrix);
		m_inverseViewDirty = false;
	}
	return pData->m_inverseViewMatrix;
}

void XM_CALLCONV Camera::SetProjection(float fov, float aspect, float zNear, float zFar)
{
	m_fov = fov;
	m_aspect = aspect;
	m_zNear = zNear;
	m_zFar = zFar;

	m_projectionDirty = true;
	m_inverseProjectionDirty = true;
}

DirectX::XMMATRIX Camera::GetProjectionMatrix() const
{
	if (m_projectionDirty)
	{
		UpdateProjectionMatrix();
	}
	return pData->m_projectionMatrix;
}

DirectX::XMMATRIX Camera::GetInverseProjectionMatrix() const
{
	if (m_inverseProjectionDirty)
	{
		UpdateInverseProjectionMatrix();
	}
	return pData->m_inverseProjectionMatrix;
}

void Camera::SetFov(float fov)
{
	// TODO: Should be if the fov passed is not the same as what is set
	// checking != is bad for floats.. this should be a better alternative
	if (m_fov - fov > std::numeric_limits<float>::epsilon())
	{
		m_fov = fov;
		m_projectionDirty = true;
		m_inverseProjectionDirty = true;
	}
}

float Camera::GetFov() const
{
	return m_fov;
}

void XM_CALLCONV Camera::SetTranslation(const DirectX::FXMVECTOR& translate) const
{
	pData->m_translation = translate;
	m_viewDirty = true;
}

DirectX::XMVECTOR Camera::GetTranslation() const
{
	return pData->m_translation;
}

void XM_CALLCONV Camera::SetRotation(const DirectX::FXMVECTOR& rotation) const
{
	pData->m_rotation = rotation;
}

DirectX::XMVECTOR Camera::GetRotation() const
{
	return pData->m_rotation;
}

void XM_CALLCONV Camera::SetFocalPoint(DirectX::FXMVECTOR focalPoint)
{
	pData->m_focalPoint = focalPoint;
	m_viewDirty = true;
}

DirectX::XMVECTOR Camera::GetFocalPoint() const
{
	return pData->m_focalPoint;
}


void XM_CALLCONV Camera::Translate(const DirectX::FXMVECTOR& translation, Space space) const
{
	switch (space)
	{
		case Space::LOCAL:
		{

			pData->m_translation = DirectX::XMVectorAdd(pData->m_translation, DirectX::XMVector3Rotate(translation, pData->m_rotation));
		}
		break;

		case Space::WORLD:
		{
			pData->m_translation = DirectX::XMVectorAdd(pData->m_translation, translation);
		}
		break;
	}

	pData->m_translation = DirectX::XMVectorSetW(pData->m_translation, 1.0f);
	m_viewDirty = true;
	m_inverseViewDirty = true;
}

void XM_CALLCONV Camera::Rotate(DirectX::FXMVECTOR& quaternion) const
{
	pData->m_rotation = DirectX::XMQuaternionMultiply(pData->m_rotation, quaternion);
	m_viewDirty = true;
	m_inverseViewDirty = true;
}

void Camera::UpdateViewMatrix() const
{
	DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixRotationQuaternion(pData->m_rotation));
	DirectX::XMMATRIX translationMatrix = DirectX::XMMatrixTranslationFromVector( DirectX::XMVectorNegate(pData->m_translation));

	pData->m_viewMatrix = translationMatrix * rotationMatrix;

	m_inverseViewDirty = true;
	m_viewDirty = true;
}

void Camera::UpdateInverseViewMatrix() const
{
	if (m_viewDirty)
	{
		UpdateViewMatrix();
	}

	pData->m_inverseViewMatrix = DirectX::XMMatrixInverse(nullptr, pData->m_viewMatrix);
	m_inverseViewDirty = false;
}

void Camera::UpdateProjectionMatrix() const
{
	pData->m_projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(m_fov), m_aspect, m_zNear, m_zFar);
	
	m_projectionDirty = false;
	m_inverseProjectionDirty = true;
}

void Camera::UpdateInverseProjectionMatrix() const
{
	if (m_projectionDirty)
	{
		UpdateProjectionMatrix();
	}

	pData->m_inverseProjectionMatrix = DirectX::XMMatrixInverse(nullptr, pData->m_projectionMatrix);
	m_inverseProjectionDirty = false;
}

