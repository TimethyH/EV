#pragma once
#include "DirectXMath.h"

// Inspired from Jeremiah van Oosten's DX12 tutorial.

namespace EV
{
	class CommandList;

	// Defines in what space the matrix transformations happen
	enum class Space
	{
		LOCAL,
		WORLD
	};

	class Camera
	{
	public:
		Camera();
		virtual ~Camera();

		/*

		Apparently consegutive Vector parameters should be names differently, starting with:
		FXM, GXM and lastly HXM like so:

		void XM_CALLCONV set_LookAt(
		DirectX::FXMVECTOR eye,
		DirectX::GXMVECTOR target,
		DirectX::HXMVECTOR up);

		*/
		void XM_CALLCONV SetLookAt(const DirectX::FXMVECTOR& position, const DirectX::FXMVECTOR& direction, const DirectX::FXMVECTOR& up) const;
		DirectX::XMMATRIX GetViewMatrix() const;
		DirectX::XMMATRIX GetInverseViewMatrix() const;

		void XM_CALLCONV SetProjection(float fov, float aspect, float zNear, float zFar);
		DirectX::XMMATRIX GetProjectionMatrix() const;
		DirectX::XMMATRIX GetInverseProjectionMatrix() const;

		// Values in Degrees.
		void SetFov(float fov);
		float GetFov() const;

		void XM_CALLCONV SetTranslation(const DirectX::FXMVECTOR& translate) const;
		DirectX::XMVECTOR GetTranslation() const;

		void XM_CALLCONV SetRotation(const DirectX::FXMVECTOR& rotation) const;
		DirectX::XMVECTOR GetRotation() const;
		void __vectorcall SetFocalPoint(DirectX::FXMVECTOR focalPoint);
		DirectX::XMVECTOR GetFocalPoint() const;

		void XM_CALLCONV Translate(const DirectX::FXMVECTOR& translation, Space space = Space::LOCAL) const;
		void XM_CALLCONV Rotate(const DirectX::FXMVECTOR& quaternion) const;

	protected:
		virtual void UpdateViewMatrix() const;
		virtual void UpdateInverseViewMatrix() const;
		virtual void UpdateProjectionMatrix() const;
		virtual void UpdateInverseProjectionMatrix() const;

		// the struct must be aligned, if not SSE intrinsics will fail.
		__declspec(align(16)) struct AlignedData
		{
			DirectX::XMVECTOR m_translation;
			DirectX::XMVECTOR m_rotation; // quaternion

			DirectX::XMMATRIX m_viewMatrix;
			DirectX::XMMATRIX m_inverseViewMatrix;
			DirectX::XMMATRIX m_projectionMatrix;
			DirectX::XMMATRIX m_inverseProjectionMatrix;

			// World-space position of the focus object.
			DirectX::XMVECTOR m_focalPoint;
		};

		AlignedData* pData = nullptr; // TODO: make this a smart pointer?

		// Projection Parameters

		float m_fov = 0;
		float m_aspect = 0;
		float m_zNear = 0;
		float m_zFar = 0;

		// Camera data
		float m_forward;
		float m_backward;
		float m_left;
		float m_right;
		float m_up;
		float m_down;

		float m_pitch;
		float m_yaw;

		mutable bool m_viewDirty = false;
		mutable bool m_inverseViewDirty = false;

		mutable bool m_projectionDirty = false;
		mutable bool m_inverseProjectionDirty = false;


	private:

	};
}