#pragma once
#include "Graphics.h"
#include "ConditionalNoexcept.h"

namespace Bind {

	class Bindable
	{
	public:
		virtual void Bind(Graphics& gfx) noexcept = 0;
		virtual ~Bindable() = default;
	protected:
		// In Graphics this base class is a friend.
		// It does not mean every child of this base class is also a friend of Graphics.
		// Make some private members of Graphics available to the children of Bindable: 
		static ID3D11DeviceContext* GetContext(Graphics& gfx) noexcept;
		static ID3D11Device* GetDevice(Graphics& gfx) noexcept;

		static DxgiInfoManager& GetInfoManager(Graphics& gfx);
	};

}