#include "Drawable.h"
#include "GraphicsThrowMacros.h"
#include "IndexBuffer.h"

#include <cassert>

using namespace Bind;

void Drawable::Draw(Graphics& gfx) const noxnd
{
	for (auto& b : binds)
	{
		b->Bind(gfx);
	}

	for (auto& b : GetStaticBinds())
	{
		b->Bind(gfx);
	}

	gfx.DrawIndexed(pIndexBuffer->GetCount());
}

/// @brief Uses unique pointer because of polymorphism.
/// @param bind : Pointer to bindable.
void Drawable::AddBind(std::unique_ptr<Bind::Bindable> bind) noxnd
{
	assert("*Must* use AddIndexBuffer to bind index buffer" && typeid(*bind) != typeid(IndexBuffer));
	binds.push_back(std::move(bind));
}

void Drawable::AddIndexBuffer(std::unique_ptr<IndexBuffer> ibuf) noxnd
{
	assert("Attempting to add index buffer a second time" && pIndexBuffer == nullptr);
	pIndexBuffer = ibuf.get();
	binds.push_back(std::move(ibuf));
}
