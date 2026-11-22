#pragma once
#include "DrawableBase.h"
#include "MathBase.h"

struct TestObjectMotion
{
	float r = 0.0f;
	float roll = 0.0f;
	float pitch = 0.0f;
	float yaw = 0.0f;
	float theta = 0.0f;
	float phi = 0.0f;
	float chi = 0.0f;

	float droll = 0.0f;
	float dpitch = 0.0f;
	float dyaw = 0.0f;
	float dtheta = 0.0f;
	float dphi = 0.0f;
	float dchi = 0.0f;
};

template<class T>
class TestObject : public DrawableBase<T>
{
public:
	TestObject(Graphics& gfx, std::mt19937& rng,
		std::uniform_real_distribution<float>& adist,
		std::uniform_real_distribution<float>& ddist,
		std::uniform_real_distribution<float>& odist,
		std::uniform_real_distribution<float>& rdist)
	{
		motion.r = rdist(rng);

		motion.droll = ddist(rng);
		motion.dpitch = ddist(rng);
		motion.dyaw = ddist(rng);

		motion.dphi = odist(rng);
		motion.dtheta = odist(rng);
		motion.dchi = odist(rng);

		motion.chi = adist(rng);
		motion.theta = adist(rng);
		motion.phi = adist(rng);
	}

	void Update(float dt) noexcept
	{
		motion.roll = wrap_angle(motion.roll + motion.droll * dt);
		motion.pitch = wrap_angle(motion.pitch + motion.dpitch * dt);
		motion.yaw = wrap_angle(motion.yaw + motion.dyaw * dt);
		motion.theta = wrap_angle(motion.theta + motion.dtheta * dt);
		motion.phi = wrap_angle(motion.phi + motion.dphi * dt);
		motion.chi = wrap_angle(motion.chi + motion.dchi * dt);
	}

	DirectX::XMMATRIX GetTransformXM() const noexcept
	{
		namespace dx = DirectX;
		return dx::XMMatrixRotationRollPitchYaw(motion.pitch, motion.yaw, motion.roll) *
			dx::XMMatrixTranslation(motion.r, 0.0f, 0.0f) *
			dx::XMMatrixRotationRollPitchYaw(motion.theta, motion.phi, motion.chi);
	}
protected:
	TestObjectMotion motion;
};
