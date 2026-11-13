//#include "../../Header Files/Bindable/TransformCbuf.h"
#include "TransformCbuf.h"

namespace Bind {

	//TransformCbuf::TransformCbuf(Graphics& gfx, const Drawable& parent) : parent(parent)
	TransformCbuf::TransformCbuf(Graphics& gfx, const Drawable& parent, UINT slot) : parent(parent)
	{
		if (!pVcbuf)
		{
			//pVcbuf = std::make_unique<VertexConstantBuffer<DirectX::XMMATRIX>>(gfx);
			//pVcbuf = std::make_unique<VertexConstantBuffer<Transforms>>(gfx);
			pVcbuf = std::make_unique<VertexConstantBuffer<Transforms>>(gfx, slot);
		}
	}

	//void TransformCbuf::Bind(Graphics& gfx) noexcept
	//{
	//	pVcbuf->Update(gfx,
	//		DirectX::XMMatrixTranspose(
	//			//parent.GetTransformXM() * gfx.GetProjection()
	//			parent.GetTransformXM() *
	//			gfx.GetCamera() *
	//			gfx.GetProjection()
	//		)
	//	);
	//	pVcbuf->Bind(gfx);
	//}

	void TransformCbuf::Bind(Graphics& gfx) noexcept
	{
		//const auto model = parent.GetTransformXM();
		const auto modelView = parent.GetTransformXM() * gfx.GetCamera();
		const Transforms tf =
		{
			DirectX::XMMatrixTranspose(/*model*/modelView),
			DirectX::XMMatrixTranspose(
				//model *
				//gfx.GetCamera() *
				modelView *
				gfx.GetProjection()
			)
		};
		pVcbuf->Update(gfx, tf);
		pVcbuf->Bind(gfx);
	}

	//std::unique_ptr<VertexConstantBuffer<DirectX::XMMATRIX>> TransformCbuf::pVcbuf;
	std::unique_ptr<VertexConstantBuffer<TransformCbuf::Transforms>> TransformCbuf::pVcbuf;

}