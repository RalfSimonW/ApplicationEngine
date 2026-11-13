#include "SimpleTimer.h"

using namespace std::chrono;

SimpleTimer::SimpleTimer() noexcept
{
	last = steady_clock::now();
}

float SimpleTimer::Mark() noexcept
{
	const auto old = last;
	last = steady_clock::now();
	const duration<float> frameTime = last - old;
	return frameTime.count();
}

float SimpleTimer::Peek() const noexcept
{
	return duration<float>(steady_clock::now() - last).count();
}
